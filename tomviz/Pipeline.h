/******************************************************************************

  This source file is part of the tomviz project.

  Copyright Kitware, Inc.

  This source code is released under the New BSD License, (the "License").

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

******************************************************************************/
#ifndef tomvizPipeline_h
#define tomvizPipeline_h

#include <QObject>

#include "PipelineWorker.h"

#include <QFile>
#include <QFileSystemWatcher>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QScopedPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>

#include <functional>

#include <vtkImageData.h>
#include <vtkSmartPointer.h>

class pqSettings;

namespace tomviz {
class DataSource;
class Operator;
class Pipeline;
class PipelineExecutor;

namespace docker {
class DockerStopInvocation;
class DockerRunInvocation;
}

class Pipeline : public QObject
{
  Q_OBJECT
public:
  enum ExecutionMode
  {
    Threaded,
    Docker
  };
  Q_ENUM(ExecutionMode)

  class ImageFuture;

  Pipeline(DataSource* dataSource, QObject* parent = nullptr);
  ~Pipeline() override;

  // Pause the automatic execution of the pipeline
  void pause();

  // Returns true if pipeline is currently paused, false otherwise.
  bool paused();

  // Resume the automatic execution of the pipeline, will execution the
  // existing pipeline. If execute is true the entire pipeline will be executed.
  void resume(bool execute = true);

  // Resume the automatic execution of the pipeline, will execute the
  // existing pipeline starting at the given data source.
  void resume(DataSource* at);

  // Cancel execution of the pipeline. canceled is a optional callback
  // that will be called when the pipeline has been successfully canceled.
  void cancel(std::function<void()> canceled = nullptr);

  /// Return true if the pipeline is currently being executed.
  bool isRunning();

  ImageFuture* getCopyOfImagePriorTo(Operator* op);

  /// Add default modules to this pipeline.
  void addDefaultModules(DataSource* dataSource);

  /// The data source a the root of the pipeline.
  DataSource* dataSource() { return m_data; }

  /// Returns that transformed data source associated with a given
  /// data source. If no data source is provided the pipeline's root data source
  /// will be used.
  DataSource* transformedDataSource(DataSource* dataSource = nullptr);

  /// Set the execution mode to use when executing the pipeline.
  void setExecutionMode(ExecutionMode executor);

  ExecutionMode executionMode() { return m_executionMode; };

public slots:
  void execute();
  void execute(DataSource* dataSource, Operator* start = nullptr);

  void branchFinished(DataSource* start, vtkDataObject* newData);

signals:
  /// This signal is when the execution of the pipeline starts.
  void started();

  /// This signal is fired the execution of the pipeline finishes.
  void finished();

  /// This signal is fired when an operator is added.  The second argument
  /// is the datasource that should be moved to become its output in the
  /// pipeline view (or null if there isn't one).
  void operatorAdded(Operator* op, DataSource* outputDS = nullptr);

private:
  DataSource* findTransformedDataSource(DataSource* dataSource);
  Operator* findTransformedDataSourceOperator(DataSource* dataSource);
  void addDataSource(DataSource* dataSource);

  DataSource* m_data;
  bool m_paused = false;
  QScopedPointer<PipelineExecutor> m_executor;
  ExecutionMode m_executionMode = Threaded;
};

/// Return from getCopyOfImagePriorTo for caller to track async operation.
class Pipeline::ImageFuture : public QObject
{
  Q_OBJECT

public:
  friend class ThreadPipelineExecutor;

  vtkSmartPointer<vtkImageData> result() { return m_imageData; }
  Operator* op() { return m_operator; }

signals:
  void finished(bool result);
  void canceled();

private:
  ImageFuture(Operator* op, vtkImageData* imageData,
              PipelineWorker::Future* future = nullptr,
              QObject* parent = nullptr);
  ~ImageFuture() override;
  Operator* m_operator;
  vtkSmartPointer<vtkImageData> m_imageData;
  PipelineWorker::Future* m_future;
};

class PipelineSettings
{
public:
  PipelineSettings();
  Pipeline::ExecutionMode executionMode();
  QString dockerImage();
  bool dockerPull();
  bool dockerRemove();

  void setExecutionMode(Pipeline::ExecutionMode executor);
  void setExecutionMode(const QString& executor);
  void setDockerImage(const QString& image);
  void setDockerPull(bool pull);
  void setDockerRemove(bool remove);

private:
  pqSettings* m_settings;
};


} // namespace tomviz

#endif // tomvizPipeline_h
