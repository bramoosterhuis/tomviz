/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "SaveDataReaction.h"

#include "ConvertToFloatOperator.h"
#include "EmdFormat.h"
#include "Utilities.h"

#include "ActiveObjects.h"
#include "DataSource.h"
#include "FileFormatManager.h"
#include "ModuleManager.h"
#include "PythonWriter.h"

#include <pqActiveObjects.h>
#include <pqPipelineSource.h>
#include <pqProxyWidgetDialog.h>
#include <pqSaveDataReaction.h>
#include <vtkSMCoreUtilities.h>
#include <vtkSMParaViewPipelineController.h>
#include <vtkSMPropertyHelper.h>
#include <vtkSMProxyManager.h>
#include <vtkSMSessionProxyManager.h>
#include <vtkSMSourceProxy.h>
#include <vtkSMWriterFactory.h>

#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkTIFFWriter.h>
#include <vtkTrivialProducer.h>

#include <cassert>

#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace tomviz {

SaveDataReaction::SaveDataReaction(QAction* parentObject)
  : pqReaction(parentObject)
{
  connect(&ActiveObjects::instance(), SIGNAL(dataSourceChanged(DataSource*)),
          SLOT(updateEnableState()));
  updateEnableState();
}

void SaveDataReaction::updateEnableState()
{
  parentAction()->setEnabled(ActiveObjects::instance().activeDataSource() !=
                             nullptr);
}

void SaveDataReaction::onTriggered()
{
  FileFormatManager::instance().registerPythonWriters();
  QStringList filters;
  filters << "TIFF format (*.tiff)"
          << "EMD format (*.emd *.hdf5)"
          << "CSV File (*.csv)"
          << "Exodus II File (*.e *.ex2 *.ex2v2 *.exo *.exoII *.exoii *.g)"
          << "Legacy VTK Files (*.vtk)"
          << "Meta Image Files (*.mhd)"
          << "ParaView Data Files (*.pvd)"
          << "VTK ImageData Files (*.vti)"
          << "XDMF Data File (*.xmf)"
          << "JSON Image Files (*.json)";

  foreach (auto writer, FileFormatManager::instance().pythonWriterFactories()) {
    filters << writer->getFileDialogFilter();
  }

  QFileDialog dialog(nullptr);
  dialog.setFileMode(QFileDialog::AnyFile);
  dialog.setNameFilters(filters);
  dialog.setObjectName("FileOpenDialog-tomviz"); // avoid name collision?
  dialog.setAcceptMode(QFileDialog::AcceptSave);

  if (dialog.exec() == QDialog::Accepted) {
    QStringList filenames = dialog.selectedFiles();
    QString format = dialog.selectedNameFilter();
    QString filename = filenames[0];
    int startPos = format.indexOf("(") + 1;
    int n = format.indexOf(")") - startPos;
    QString extensionString = format.mid(startPos, n);
    QStringList extensions = extensionString.split(QRegularExpression(" ?\\*"),
                                                   QString::SkipEmptyParts);
    bool hasExtension = false;
    for (QString& str : extensions) {
      if (filename.endsWith(str)) {
        hasExtension = true;
      }
    }
    if (!hasExtension) {
      filename = QString("%1%2").arg(filename, extensions[0]);
    }
    saveData(filename);
  }
}

bool SaveDataReaction::saveData(const QString& filename)
{
  auto server = pqActiveObjects::instance().activeServer();
  auto source = ActiveObjects::instance().activeDataSource();
  auto result = ActiveObjects::instance().activeOperatorResult();

  auto updateSource = [](QString fileName, DataSource* ds) {
    ds->setPersistenceState(DataSource::PersistenceState::Saved);
    ds->setFileName(fileName);
  };

  if (!server) {
    qCritical("No active server located.");
    return false;
  }

  if (!source && !result) {
    qCritical("No active source located.");
    return false;
  }

  FileFormatManager::instance().registerPythonWriters();

  QFileInfo info(filename);
  if (info.suffix() == "emd") {
    EmdFormat writer;
    if (!writer.write(filename.toLatin1().data(), source)) {
      qCritical() << "Failed to write out data.";
      return false;
    } else {
      updateSource(filename, source);
      return true;
    }
  } else if (FileFormatManager::instance().pythonWriterFactory(
               info.suffix().toLower()) != nullptr) {
    auto factory = FileFormatManager::instance().pythonWriterFactory(
      info.suffix().toLower());
    Q_ASSERT(factory != nullptr);
    auto writer = factory->createWriter();
    auto t = source->producer();
    auto data = vtkImageData::SafeDownCast(t->GetOutputDataObject(0));
    if (!writer.write(filename, data)) {
      qCritical() << "Failed to write out data.";
      return false;
    } else {
      updateSource(filename, source);
      return true;
    }
  }

  vtkSMSourceProxy* producer = nullptr;
  if (source) {
    producer = source->proxy();
  }
  // If an operator result is active, save it. Otherwise, save the source.
  if (result) {
    producer = result->producerProxy();
  }

  auto writerFactory = vtkSMProxyManager::GetProxyManager()->GetWriterFactory();
  vtkSmartPointer<vtkSMProxy> proxy;
  proxy.TakeReference(
    writerFactory->CreateWriter(filename.toLatin1().data(), producer));
  auto writer = vtkSMSourceProxy::SafeDownCast(proxy);
  if (!writer) {
    qCritical() << "Failed to create writer for: " << filename;
    return false;
  }

  // Convert to float if the type is found to be a double.
  if (strcmp(writer->GetClientSideObject()->GetClassName(), "vtkTIFFWriter") ==
      0) {
    auto t = vtkTrivialProducer::SafeDownCast(producer->GetClientSideObject());
    auto imageData = vtkImageData::SafeDownCast(t->GetOutputDataObject(0));
    if (imageData->GetPointData()->GetScalars()->GetDataType() == VTK_DOUBLE) {
      vtkNew<vtkImageData> fImage;
      fImage->DeepCopy(imageData);
      ConvertToFloatOperator convertFloat;
      convertFloat.applyTransform(fImage);

      vtkNew<vtkTIFFWriter> tiff;
      tiff->SetInputData(fImage);
      tiff->SetFileName(filename.toLatin1().data());
      tiff->Write();

      updateSource(filename, source);
      return true;
    }
  }

  pqProxyWidgetDialog dialog(writer, tomviz::mainWidget());
  dialog.setObjectName("WriterSettingsDialog");
  dialog.setEnableSearchBar(true);
  dialog.setWindowTitle(
    QString("Configure Writer (%1)").arg(writer->GetXMLLabel()));

  // Check to see if this writer has any properties that can be configured by
  // the user. If it does, display the dialog.
  if (dialog.hasVisibleWidgets()) {
    dialog.exec();
    if (dialog.result() == QDialog::Rejected) {
      // The user pressed Cancel so don't write
      return false;
    }
  }
  writer->UpdateVTKObjects();
  writer->UpdatePipeline();

  updateSource(filename, source);

  return true;
}

} // end of namespace tomviz
