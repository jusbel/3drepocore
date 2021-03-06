/**
 *  Copyright (C) 2015 3D Repo Ltd
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as
 *  published by the Free Software Foundation, either version 3 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "repo_pca.h"
#include <iostream>

repo::core::RepoPCA::RepoPCA() {}

repo::core::RepoPCA::~RepoPCA() {}

void repo::core::RepoPCA::initialize(
        const std::vector<RepoVertex>& xyzVertices)
{
	//std::set<RepoVertex> vertices;
	//for each (const RepoVertex& v in vert)
	//	vertices.insert(v);

    //--------------------------------------------------------------------------
	// Calculate the xyzMean vertex and the midpoint of the dataset
	// See http://en.wikipedia.org/wiki/Sample_mean_and_sample_covariance#Weighted_samples
	// Weighted mean = (sum w_i x_i) / sum w_i
	double sumOfWeights = 0;
    for (unsigned int i = 0; i < xyzVertices.size(); ++i)
    {
        RepoVertex v = xyzVertices[i];
		xyzMean += RepoVertex(v * (float) v.weight);	
		sumOfWeights += v.weight;		
	}
	xyzMean /= (float) sumOfWeights; //(float) vertices.size();	
	
    //--------------------------------------------------------------------------
	// Eigenvalue decomposition of the covariance matrix
	double eigenVectors[3][3];
	double eigenValues[3];
	// Calculated eigenValues (and vectors) are in ascending order.
	// Eigenvectors are in the respective columns.
    aiMatrix3x3 covarianceMatrix = RepoEigen::covarianceMatrix(xyzVertices, xyzMean);
	RepoEigen::eigenvalueDecomposition(covarianceMatrix, eigenVectors, eigenValues);

    //--------------------------------------------------------------------------
	// Store eigenVectors and eigenValues.
	principalComponents.resize(RepoVertex::DIMENSIONALITY);
	for (unsigned int i = 0; i < principalComponents.size(); ++i)
	{
		unsigned int index = RepoVertex::DIMENSIONALITY - i - 1;
		for (unsigned int xyz = 0; xyz < RepoVertex::DIMENSIONALITY; ++ xyz)
			principalComponents[i].vector[xyz] = (float) eigenVectors[xyz][index]; 
		principalComponents[i].vector.Normalize();
		principalComponents[i].value = eigenValues[index];
	}
	
    //--------------------------------------------------------------------------
	// Get the rotation matrix to transform from XYZ to eigenVectors UVW space.
	// See http://ocw.mit.edu/courses/aeronautics-and-astronautics/16-07-dynamics-fall-2009/lecture-notes/MIT16_07F09_Lec03.pdf
	// page 10.
    uvwRotationMatrix.a1 = principalComponents[U].vector.x;// * RepoVertex::X_AXIS;
    uvwRotationMatrix.a2 = principalComponents[U].vector.y;// * RepoVertex::Y_AXIS;
    uvwRotationMatrix.a3 = principalComponents[U].vector.z;// * RepoVertex::Z_AXIS;

    uvwRotationMatrix.b1 = principalComponents[V].vector.x;// * RepoVertex::X_AXIS;
    uvwRotationMatrix.b2 = principalComponents[V].vector.y;// * RepoVertex::Y_AXIS;
    uvwRotationMatrix.b3 = principalComponents[V].vector.z;// * RepoVertex::Z_AXIS;

    uvwRotationMatrix.c1 = principalComponents[W].vector.x;// * RepoVertex::X_AXIS;
    uvwRotationMatrix.c2 = principalComponents[W].vector.y;// * RepoVertex::Y_AXIS;
    uvwRotationMatrix.c3 = principalComponents[W].vector.z;// * RepoVertex::Z_AXIS;

    //--------------------------------------------------------------------------
	// The inverse rotation, in this particular case T^-1 == T'
    xyzRotationMatrix = aiMatrix3x3t<float>(uvwRotationMatrix).Transpose();
	
    //--------------------------------------------------------------------------
	// Rotate the vertices around the xyzMean to the UVW space to get the bbox.
    uvwMin = RepoVertex(RepoVertex::getMaxVertex<float>());
    uvwMax = RepoVertex(RepoVertex::getMinVertex<float>());


    //uvwVertices.reserve(vertices.size());
    sumOfWeights = 0;
    for (unsigned int i = 0; i < xyzVertices.size(); ++i)
    {
        RepoVertex uvwVertex = transformToUVW(xyzVertices[i]);
        uvwVertex.updateMinMax(uvwMin, uvwMax);
        uvwVertices.push_back(uvwVertex);

        uvwMean += RepoVertex(uvwVertex * (float) uvwVertex.weight);
        sumOfWeights += uvwVertex.weight;
    }
    uvwMean /= (float) sumOfWeights; //(float) vertices.size();
	
    //--------------------------------------------------------------------------
	// Store the lengths of the eigenvector oriented bounding box and calculate
	// the new midpoint in UVW.
	for (unsigned int i = 0; i < RepoVertex::DIMENSIONALITY; ++i)
	{
		// Bbox length along one of the UVW axes.
		double length = std::abs(uvwMax[i] - uvwMin[i]);
		principalComponents[i].magnitude = length;
		uvwCentroid[i] = (float) (uvwMin[i] + length/2.0f);
	}
	
    //--------------------------------------------------------------------------
	// Transformed centroid in UVW is the centroid of the PCA bbox in XYZ.
	xyzCentroid = transformToXYZ(uvwCentroid); 
    xyzMin = transformToXYZ(uvwMin);
    xyzMax = transformToXYZ(uvwMax);
}

repo::core::RepoPrincipalComponent repo::core::RepoPCA::getPrincipalComponent(
	const repo::core::RepoPCA::Basis& basis) const
{
	return (0 <= basis && basis < principalComponents.size()) 
		? principalComponents[basis] 
		: RepoPrincipalComponent();
}

double repo::core::RepoPCA::planarityError(
        const std::vector<RepoVertex>& xyzVertices) const
{
    //--------------------------------------------------------------------------
	// Eigenvector corresponding to the smallest eigenvalue is the normal of 
	// the plane defined by the xyzMean point.
	RepoVertex normal = principalComponents[W].vector;
	normal.Normalize();

	//-------------------------------------------------------------------------
	// Calculate the L^2 error of all points from the plane defined by the
	// average 3D point 'xyzMean' and its 'normal' vector.
	double error = 0; 
    for (unsigned int i = 0; i < xyzVertices.size(); ++i)
	{
        RepoVertex v = xyzVertices[i];
        RepoVertex diff(v - xyzMean);
		double a = normal * diff;
		error += a * a;
	}

    //--------------------------------------------------------------------------
	// Normalize
	//error /= vertices.size();

	return error;
}

double repo::core::RepoPCA::l2Distance(const RepoPCA& pca) const
{
	const double deltaU = principalComponents[U].magnitude - pca.getPrincipalComponent(U).magnitude;
	const double deltaV = principalComponents[V].magnitude - pca.getPrincipalComponent(V).magnitude;
	const double deltaW = principalComponents[W].magnitude - pca.getPrincipalComponent(W).magnitude;
	return std::sqrt(deltaU * deltaU + deltaV * deltaV + deltaW * deltaW);
}

double repo::core::RepoPCA::volume() const
{
	return 
		principalComponents[U].magnitude *
		principalComponents[V].magnitude *
		principalComponents[W].magnitude;
}

bool repo::core::RepoPCA::contains(const RepoVertex & vertex) const
{
	RepoVertex uvwVertex = transformToUVW(vertex);
	return 
		(uvwVertex.x > (uvwCentroid.x - principalComponents[U].magnitude / 2)) &&
		(uvwVertex.x < (uvwCentroid.x + principalComponents[U].magnitude / 2)) &&
		(uvwVertex.y > (uvwCentroid.y - principalComponents[V].magnitude / 2)) &&
		(uvwVertex.y < (uvwCentroid.y + principalComponents[V].magnitude / 2)) &&
		(uvwVertex.z > (uvwCentroid.z - principalComponents[W].magnitude / 2)) &&
		(uvwVertex.z < (uvwCentroid.z + principalComponents[W].magnitude / 2));
}

//------------------------------------------------------------------------------
//
// Transformations
//
//------------------------------------------------------------------------------

std::vector<double> repo::core::RepoPCA::getXYZTransformationMatrix() const
{
	// Assimp is row-major while GLC is plain weird. GLC seems to be row-major
	// however the translation is in the last row of the transformation matrix
	// rather than the last column.
	// See the bottom of to get the idea
	// http://www.ia.hiof.no/~borres/cgraph/math/threed/p-threed.html
    std::vector<double> transformation(16);

    aiMatrix3x3t<float>tempTranspose = aiMatrix3x3t<float>(xyzRotationMatrix).Transpose();

    transformation[0] = tempTranspose.a1;
    transformation[1] = tempTranspose.a2;
    transformation[2] = tempTranspose.a3;
    transformation[3] = 0;

    transformation[4] = tempTranspose.b1;
    transformation[5] = tempTranspose.b2;
    transformation[6] = tempTranspose.b3;
    transformation[7] = 0;

    transformation[8] = tempTranspose.c1;
    transformation[9] = tempTranspose.c2;
    transformation[10] = tempTranspose.c3;
    transformation[11] = 0;

    transformation[12] = xyzCentroid.x;
    transformation[13] = xyzCentroid.y;
    transformation[14] = xyzCentroid.z;
    transformation[15] = 1;
	
    return transformation;
}

repo::core::RepoVertex repo::core::RepoPCA::transformToUVW(const RepoVertex& v) 
	const
{
    return RepoVertex(uvwRotationMatrix * (v - xyzMean));
}

repo::core::RepoVertex repo::core::RepoPCA::transformToXYZ(const RepoVertex& v)
	const
{
    return RepoVertex((xyzRotationMatrix * v) + xyzMean);
}
