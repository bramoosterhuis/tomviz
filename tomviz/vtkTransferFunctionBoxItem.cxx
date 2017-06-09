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
#include "vtkTransferFunctionBoxItem.h"
#include <vtkBrush.h>
#include <vtkColorTransferFunction.h>
#include <vtkContext2D.h>
#include <vtkContextMouseEvent.h>
#include <vtkContextScene.h>
#include <vtkImageData.h>
#include <vtkObjectFactory.h>
#include <vtkPen.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPointData.h>
#include <vtkPoints2D.h>
#include <vtkUnsignedCharArray.h>
#include <vtkVectorOperators.h>

namespace {
inline bool PointIsWithinBounds2D(double point[2], double bounds[4],
                           const double delta[2])
{
  if (!point || !bounds || !delta) {
    return false;
  }

  for (int i = 0; i < 2; i++) {
    if (point[i] + delta[i] < bounds[2 * i] ||
        point[i] - delta[i] > bounds[2 * i + 1]) {
      return false;
    }
  }
  return true;
}
}

////////////////////////////////////////////////////////////////////////////////
vtkStandardNewMacro(vtkTransferFunctionBoxItem)

  vtkTransferFunctionBoxItem::vtkTransferFunctionBoxItem()
  : Superclass()
{
  // Initialize box, points are ordered as:
  //     3 ----- 2
  //     |       |
  // (4) 0 ----- 1
  this->AddPoint(1.0, 1.0);
  this->AddPoint(20.0, 1.0);
  this->AddPoint(20.0, 20.0);
  this->AddPoint(1.0, 20.0);

  // Point 0 is repeated for rendering purposes.
  this->BoxPoints->InsertNextPoint(1.0, 1.0);

  // Rendering setup
  this->Pen->SetWidth(2.);
  this->Pen->SetColor(255, 255, 255);
  this->Pen->SetLineType(vtkPen::SOLID_LINE);

  const int texSize = 256;
  this->Texture->SetDimensions(texSize, 1, 1);
  this->Texture->AllocateScalars(VTK_UNSIGNED_CHAR, 4);
}

vtkTransferFunctionBoxItem::~vtkTransferFunctionBoxItem() = default;

void vtkTransferFunctionBoxItem::DragBox(const double deltaX,
                                         const double deltaY)
{
  this->StartChanges();

  if (!BoxIsWithinBounds(deltaX, deltaY))
    return;

  this->MovePoint(BOTTOM_LEFT, deltaX, deltaY);
  this->MovePoint(BOTTOM_LEFT_LOOP, deltaX, deltaY);
  this->MovePoint(BOTTOM_RIGHT, deltaX, deltaY);
  this->MovePoint(TOP_RIGHT, deltaX, deltaY);
  this->MovePoint(TOP_LEFT, deltaX, deltaY);

  this->EndChanges();
  this->InvokeEvent(vtkCommand::SelectionChangedEvent);
}

bool vtkTransferFunctionBoxItem::BoxIsWithinBounds(const double deltaX,
                                                   const double deltaY)
{
  double bounds[4];
  this->GetValidBounds(bounds);

  const double delta[2] = { 0.0, 0.0 };
  for (vtkIdType id = 0; id < this->NumPoints; id++) {
    double pos[2];
    this->BoxPoints->GetPoint(id, pos);
    pos[0] += deltaX;
    pos[1] += deltaY;
    if (!PointIsWithinBounds2D(pos, bounds, delta))
      return false;
  }
  return true;
}

void vtkTransferFunctionBoxItem::MovePoint(vtkIdType pointId,
                                           const double deltaX,
                                           const double deltaY)
{
  double pos[2];
  this->BoxPoints->GetPoint(pointId, pos);

  double newPos[2] = { pos[0] + deltaX, pos[1] + deltaY };
  this->ClampToValidPosition(newPos);

  this->BoxPoints->SetPoint(pointId, newPos[0], newPos[1]);
}

vtkIdType vtkTransferFunctionBoxItem::AddPoint(const double x, const double y)
{
  double pos[2] = { x, y };
  return this->AddPoint(pos);
}

vtkIdType vtkTransferFunctionBoxItem::AddPoint(double* pos)
{
  if (this->BoxPoints->GetNumberOfPoints() >= 4) {
    return 3;
  }

  this->StartChanges();

  const vtkIdType id = this->BoxPoints->InsertNextPoint(pos[0], pos[1]);
  Superclass::AddPointId(id);

  this->EndChanges();

  return id;
}

void vtkTransferFunctionBoxItem::DragCorner(const vtkIdType cornerId,
                                            const double* delta)
{
  if (cornerId < 0 || cornerId > 3) {
    return;
  }

  this->StartChanges();

  // Move dragged corner and adjacent corners
  switch (cornerId) {
    case BOTTOM_LEFT:
      if (this->ArePointsCrossing(cornerId, delta, TOP_RIGHT))
        return;
      this->MovePoint(cornerId, delta[0], delta[1]);
      this->MovePoint(BOTTOM_LEFT_LOOP, delta[0], delta[1]);
      this->MovePoint(TOP_LEFT, delta[0], 0.0);
      this->MovePoint(BOTTOM_RIGHT, 0.0, delta[1]);
      break;

    case BOTTOM_RIGHT:
      if (this->ArePointsCrossing(cornerId, delta, TOP_LEFT))
        return;
      this->MovePoint(cornerId, delta[0], delta[1]);
      this->MovePoint(BOTTOM_LEFT, 0.0, delta[1]);
      this->MovePoint(BOTTOM_LEFT_LOOP, 0.0, delta[1]);
      this->MovePoint(TOP_RIGHT, delta[0], 0.0);
      break;

    case TOP_RIGHT:
      if (this->ArePointsCrossing(cornerId, delta, BOTTOM_LEFT))
        return;
      this->MovePoint(cornerId, delta[0], delta[1]);
      this->MovePoint(BOTTOM_RIGHT, delta[0], 0.0);
      this->MovePoint(TOP_LEFT, 0.0, delta[1]);
      break;

    case TOP_LEFT:
      if (this->ArePointsCrossing(cornerId, delta, BOTTOM_RIGHT))
        return;
      this->MovePoint(cornerId, delta[0], delta[1]);
      this->MovePoint(TOP_RIGHT, 0.0, delta[1]);
      this->MovePoint(BOTTOM_LEFT, delta[0], 0.0);
      this->MovePoint(BOTTOM_LEFT_LOOP, delta[0], 0.0);
      break;
  }

  this->EndChanges();
  this->InvokeEvent(vtkCommand::SelectionChangedEvent);
}

bool vtkTransferFunctionBoxItem::ArePointsCrossing(const vtkIdType pointA,
                                                   const double* deltaA,
                                                   const vtkIdType pointB)
{
  double posA[2];
  this->BoxPoints->GetPoint(pointA, posA);

  double posB[2];
  this->BoxPoints->GetPoint(pointB, posB);

  const double distXBefore = posA[0] - posB[0];
  const double distXAfter = posA[0] + deltaA[0] - posB[0];
  if (distXAfter * distXBefore < 0.0) // Sign changed
    return true;

  const double distYBefore = posA[1] - posB[1];
  const double distYAfter = posA[1] + deltaA[1] - posB[1];
  if (distYAfter * distYBefore < 0.0) // Sign changed
    return true;

  return false;
}

vtkIdType vtkTransferFunctionBoxItem::RemovePoint(double* pos)
{
  // This method does nothing as this item has a fixed number of points (4).
  return 0;
}

void vtkTransferFunctionBoxItem::SetControlPoint(vtkIdType index, double* point)
{
  // This method does nothing as this item has a fixed number of points (4).
  return;
}

vtkIdType vtkTransferFunctionBoxItem::GetNumberOfPoints() const
{
  return static_cast<vtkIdType>(this->NumPoints);
}

void vtkTransferFunctionBoxItem::GetControlPoint(vtkIdType index,
                                                 double* point) const
{
  if (index >= this->NumPoints) {
    return;
  }

  this->BoxPoints->GetPoint(index, point);
}

vtkMTimeType vtkTransferFunctionBoxItem::GetControlPointsMTime()
{
  return this->GetMTime();
}

void vtkTransferFunctionBoxItem::emitEvent(unsigned long event, void* params)
{
  this->InvokeEvent(event, params);
}

bool vtkTransferFunctionBoxItem::Paint(vtkContext2D* painter)
{
  // Prepare brush
  if (this->Texture->GetMTime() < this->GetMTime())
  {
    this->ComputeTexture();
  }

  auto brush = painter->GetBrush();
  brush->SetColorF(0.0, 0.0, 0.0, 0.0);
  brush->SetTexture(this->Texture.GetPointer());
  brush->SetTextureProperties(vtkBrush::Linear | vtkBrush::Stretch);

  // Prepare outline
  painter->ApplyPen(this->Pen.GetPointer());

  painter->DrawPolygon(this->BoxPoints.GetPointer());
  return Superclass::Paint(painter);
}

void vtkTransferFunctionBoxItem::ComputeTexture()
{
  double range[2];
  this->ColorFunction->GetRange(range);

  const int texSize = this->Texture->GetDimensions()[0];
  auto dataRGB = new double[texSize * 3];
  this->ColorFunction->GetTable(range[0], range[1], texSize, dataRGB);

  auto dataAlpha = new double[texSize];
  this->OpacityFunction->GetTable(range[0], range[1], texSize, dataAlpha);

  auto arr = vtkUnsignedCharArray::SafeDownCast(
    this->Texture->GetPointData()->GetScalars());

    for (vtkIdType i = 0; i < texSize; i++) {

      double color[4];
      color[0] = dataRGB[i * 3] * 255.0;
      color[1] = dataRGB[i * 3 + 1] * 255.0;
      color[2] = dataRGB[i * 3 + 2] * 255.0;
      color[3] = dataAlpha[i] * 255.0;

      arr->SetTuple(i, color);
    }

  delete [] dataRGB;
  delete [] dataAlpha;
}

bool vtkTransferFunctionBoxItem::Hit(const vtkContextMouseEvent& mouse)
{
  vtkVector2f vpos = mouse.GetPos();
  this->TransformScreenToData(vpos, vpos);

  double pos[2];
  pos[0] = vpos.GetX();
  pos[1] = vpos.GetY();

  double bounds[4];
  this->GetBounds(bounds);

  const double delta[2] = { 0.0, 0.0 };
  const bool isWithinBox = PointIsWithinBounds2D(pos, bounds, delta);

  // maybe the cursor is over the first or last point (which could be outside
  // the bounds because of the screen point size).
  bool isOverPoint = false;
  for (int i = 0; i < this->NumPoints; ++i) {
    isOverPoint = this->IsOverPoint(pos, i);
    if (isOverPoint) {
      break;
    }
  }

  return isWithinBox || isOverPoint;
}

bool vtkTransferFunctionBoxItem::MouseButtonPressEvent(
  const vtkContextMouseEvent& mouse)
{
  this->MouseMoved = false;
  this->PointToToggle = -1;

  vtkVector2f vpos = mouse.GetPos();
  this->TransformScreenToData(vpos, vpos);
  double pos[2];
  pos[0] = vpos.GetX();
  pos[1] = vpos.GetY();
  vtkIdType pointUnderMouse = this->FindPoint(pos);

  if (mouse.GetButton() == vtkContextMouseEvent::LEFT_BUTTON) {
    if (pointUnderMouse != -1) {
      this->SetCurrentPoint(pointUnderMouse);
      return true;
    } else {
      this->SetCurrentPoint(-1);
    }
    return true;
  }

  return false;
}

bool vtkTransferFunctionBoxItem::MouseButtonReleaseEvent(
  const vtkContextMouseEvent& mouse)
{
  return Superclass::MouseButtonReleaseEvent(mouse);
}

bool vtkTransferFunctionBoxItem::MouseDoubleClickEvent(
  const vtkContextMouseEvent& mouse)
{
  return Superclass::MouseDoubleClickEvent(mouse);
}

bool vtkTransferFunctionBoxItem::MouseMoveEvent(
  const vtkContextMouseEvent& mouse)
{
  vtkVector2f mousePos = mouse.GetPos();
  this->TransformScreenToData(mousePos, mousePos);

  switch (mouse.GetButton()) {
    case vtkContextMouseEvent::LEFT_BUTTON:
      if (this->CurrentPoint == -1) {
        // Drag box
        vtkVector2f deltaPos = mouse.GetPos() - mouse.GetLastPos();
        this->DragBox(deltaPos.GetX(), deltaPos.GetY());
        this->Scene->SetDirty(true);
        return true;
      } else {
        // Drag corner
        vtkVector2d deltaPos =
          (mouse.GetPos() - mouse.GetLastPos()).Cast<double>();
        this->DragCorner(this->CurrentPoint, deltaPos.GetData());
        this->Scene->SetDirty(true);
        return true;
      }
      break;

    default:
      break;
  }

  return false;
}

void vtkTransferFunctionBoxItem::ClampToValidPosition(double pos[2])
{
  double bounds[4];
  this->GetValidBounds(bounds);
  pos[0] = vtkMath::ClampValue(pos[0], bounds[0], bounds[1]);
  pos[1] = vtkMath::ClampValue(pos[1], bounds[2], bounds[3]);
}

bool vtkTransferFunctionBoxItem::KeyPressEvent(const vtkContextKeyEvent& key)
{
  return Superclass::KeyPressEvent(key);
}

bool vtkTransferFunctionBoxItem::KeyReleaseEvent(const vtkContextKeyEvent& key)
{
  return Superclass::KeyPressEvent(key);
}

void vtkTransferFunctionBoxItem::PrintSelf(ostream& os, vtkIndent indent)
{
  Superclass::PrintSelf(os, indent);

  os << indent << "Box [x, y, width, height]: [" << this->Box.GetX() << ", "
     << this->Box.GetY() << ", " << this->Box.GetWidth() << ", "
     << this->Box.GetHeight() << "]\n";
}

const vtkRectd& vtkTransferFunctionBoxItem::GetBox()
{
  double lowerBound[2];
  this->BoxPoints->GetPoint(BOTTOM_LEFT, lowerBound);

  double upperBound[2];
  this->BoxPoints->GetPoint(TOP_RIGHT, upperBound);

  const double width = upperBound[0] - lowerBound[0];
  const double height = upperBound[1] - lowerBound[1];

  this->Box.Set(lowerBound[0], lowerBound[1], width, height);

  return this->Box;
}

vtkCxxSetObjectMacro(vtkTransferFunctionBoxItem, ColorFunction,
                     vtkColorTransferFunction)

vtkCxxSetObjectMacro(vtkTransferFunctionBoxItem, OpacityFunction,
                       vtkPiecewiseFunction)
