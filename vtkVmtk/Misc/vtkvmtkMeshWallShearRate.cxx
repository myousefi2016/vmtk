/*=========================================================================

Program:   VMTK
Module:    $RCSfile: vtkvmtkMeshWallShearRate.cxx,v $
Language:  C++
Date:      $Date: 2020/02/17 10:20:00 $
Version:   $Revision: 2.0 $

  Copyright (c) Luca Antiga, David Steinman, Mehrdad Yousefi. All rights reserved.
  See LICENSE file for details.

  Portions of this code are covered under the VTK copyright.
  See VTKCopyright.txt or http://www.kitware.com/VTKCopyright.htm 
  for details.

     This software is distributed WITHOUT ANY WARRANTY; without even 
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/

#include "vtkvmtkMeshWallShearRate.h"

#include "vtkvmtkUnstructuredGridGradientFilter.h"

#include "vtkUnstructuredGrid.h"
#include "vtkPolyData.h"
#include "vtkPointData.h"
#include "vtkDoubleArray.h"
#include "vtkMath.h"
#include "vtkGeometryFilter.h"
#include "vtkPolyDataNormals.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkVersion.h"


vtkStandardNewMacro(vtkvmtkMeshWallShearRate);

vtkvmtkMeshWallShearRate::vtkvmtkMeshWallShearRate()
{
  this->VelocityArrayName = NULL;
  this->WallShearRateArrayName = NULL;
  this->ComputeIndividualPartialDerivatives = 0;
  this->ConvergenceTolerance = 1E-6;
  this->QuadratureOrder = 3;
}

vtkvmtkMeshWallShearRate::~vtkvmtkMeshWallShearRate()
{
  if (this->VelocityArrayName)
    {
    delete[] this->VelocityArrayName;
    this->VelocityArrayName = NULL;
    }
  if (this->WallShearRateArrayName)
    {
    delete[] this->WallShearRateArrayName;
    this->WallShearRateArrayName = NULL;
    }
}

int vtkvmtkMeshWallShearRate::FillInputPortInformation(int, vtkInformation *info)
{
  info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkUnstructuredGrid");
  return 1;
}

int vtkvmtkMeshWallShearRate::RequestData(
  vtkInformation *vtkNotUsed(request),
  vtkInformationVector **inputVector,
  vtkInformationVector *outputVector)
{
  vtkInformation *inInfo = inputVector[0]->GetInformationObject(0);
  vtkInformation *outInfo = outputVector->GetInformationObject(0);

  vtkUnstructuredGrid *input = vtkUnstructuredGrid::SafeDownCast(
    inInfo->Get(vtkDataObject::DATA_OBJECT()));
  vtkPolyData *output = vtkPolyData::SafeDownCast(
    outInfo->Get(vtkDataObject::DATA_OBJECT()));

  if (this->VelocityArrayName == NULL)
    {
    vtkErrorMacro("VelocityArrayName not specified");
    return 1;
    }

  vtkDataArray* velocityArray = input->GetPointData()->GetArray(this->VelocityArrayName); 
  
  if (velocityArray == NULL)
    {
    vtkErrorMacro("VelocityArray with name specified does not exist");
    return 1;
    }

  char gradientArrayName[] = "VelocityGradient";

  vtkvmtkUnstructuredGridGradientFilter* gradientFilter = vtkvmtkUnstructuredGridGradientFilter::New();
#if (VTK_MAJOR_VERSION <= 5)
  gradientFilter->SetInput(input);
#else
  gradientFilter->SetInputData(input);
#endif
  gradientFilter->SetInputArrayName(this->VelocityArrayName);
  gradientFilter->SetGradientArrayName(gradientArrayName);
  gradientFilter->SetQuadratureOrder(this->QuadratureOrder);
  gradientFilter->SetConvergenceTolerance(this->ConvergenceTolerance);
  gradientFilter->SetComputeIndividualPartialDerivatives(this->ComputeIndividualPartialDerivatives);
  gradientFilter->Update();

  vtkGeometryFilter* geometryFilter = vtkGeometryFilter::New();
#if (VTK_MAJOR_VERSION <= 5)
  geometryFilter->SetInput(gradientFilter->GetOutput());
#else
  geometryFilter->SetInputConnection(gradientFilter->GetOutputPort());
#endif
  geometryFilter->Update();

  vtkPolyDataNormals* normalsFilter = vtkPolyDataNormals::New();
#if (VTK_MAJOR_VERSION <= 5)
  normalsFilter->SetInput(geometryFilter->GetOutput());
#else
  normalsFilter->SetInputConnection(geometryFilter->GetOutputPort());
#endif
  normalsFilter->AutoOrientNormalsOn();
  normalsFilter->ConsistencyOn();
  normalsFilter->SplittingOff();
  normalsFilter->Update();

  vtkPolyData* outputSurface = normalsFilter->GetOutput();

  vtkDataArray* velocityGradientArray = outputSurface->GetPointData()->GetArray(gradientArrayName);
  vtkDataArray* normalsArray = outputSurface->GetPointData()->GetNormals();

  int numberOfPoints = outputSurface->GetNumberOfPoints();
  
  vtkDoubleArray* wallShearRateArray = vtkDoubleArray::New();
  if (this->WallShearRateArrayName)
    {
    wallShearRateArray->SetName(this->WallShearRateArrayName);
    }
  else
    {
    wallShearRateArray->SetName("WallShearRate");
    }
  wallShearRateArray->SetNumberOfComponents(3);
  wallShearRateArray->SetNumberOfTuples(numberOfPoints);

  double velocityGradient[9];
  double shearRateTensor[3][3];
  double normal[3];
  double wallShearRate[3];
 
/*=========================================================================

  Calculate shear rate tensor: S = 0.5 * (\nabla u + (\nabla u)^{T})
  Calculate wall shear rate vector: \vec{\tau} = 2 * (S \cdot n - (n \cdot S \cdot n) \cdot n)
  Reference: Equation A.4 from Matyka et. al. (Wall orientation and shear stress in the lattice Boltzmann model) http://dx.doi.org/10.1016/j.compfluid.2012.12.018

=========================================================================*/

  int i, j, k;
  int alpha, beta;
  for (i=0; i<numberOfPoints; i++)
    {
    velocityGradientArray->GetTuple(i,velocityGradient);
    normalsArray->GetTuple(i,normal);

    shearRateTensor[0][0] = 0.5*(velocityGradient[0] + velocityGradient[0]);
    shearRateTensor[0][1] = 0.5*(velocityGradient[1] + velocityGradient[3]);
    shearRateTensor[0][2] = 0.5*(velocityGradient[2] + velocityGradient[6]);

    shearRateTensor[1][0] = 0.5*(velocityGradient[3] + velocityGradient[1]);
    shearRateTensor[1][1] = 0.5*(velocityGradient[4] + velocityGradient[4]);
    shearRateTensor[1][2] = 0.5*(velocityGradient[5] + velocityGradient[7]);

    shearRateTensor[2][0] = 0.5*(velocityGradient[6] + velocityGradient[2]);
    shearRateTensor[2][1] = 0.5*(velocityGradient[7] + velocityGradient[5]);
    shearRateTensor[2][2] = 0.5*(velocityGradient[8] + velocityGradient[8]);

    double nSn = 0.0;

    for (alpha=0; alpha<3; alpha++)
    {
            for (beta=0; beta<3; beta++)
            {
      nSn = nSn - 2.0 * shearRateTensor[alpha][beta] * normal[alpha] * normal[beta];
            }
    }

    double nS = 0.0;

    for (j=0; j<3; j++)
    {
	    nS = 0.0;
	    for (k = 0; k < 3; k++)
	    {
		    nS = nS - 2.0 * shearRateTensor[j][k] * normal[k];
	    }

	    wallShearRate[j] = nS - nSn * normal[j];
    }
    wallShearRateArray->SetTuple(i,wallShearRate);
    }

  output->DeepCopy(outputSurface);
  output->GetPointData()->AddArray(wallShearRateArray);
  
  wallShearRateArray->Delete();
  geometryFilter->Delete();
  normalsFilter->Delete();
  
  return 1;
}

void vtkvmtkMeshWallShearRate::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);
}
