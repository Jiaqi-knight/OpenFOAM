/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2011-2018 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

Application
    surfaceFeatureExtract

Description
    Extracts and writes surface features to file. All but the basic feature
    extraction is WIP.

\*---------------------------------------------------------------------------*/

#include "surfaceFeatureExtract.H"
#include "Time.H"
#include "tensor2D.H"
#include "symmTensor2D.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

void Foam::deleteBox
(
    const triSurface& surf,
    const boundBox& bb,
    const bool removeInside,
    List<surfaceFeatures::edgeStatus>& edgeStat
)
{
    forAll(edgeStat, edgei)
    {
        const point eMid = surf.edges()[edgei].centre(surf.localPoints());

        if (removeInside ? bb.contains(eMid) : !bb.contains(eMid))
        {
            edgeStat[edgei] = surfaceFeatures::NONE;
        }
    }
}


void Foam::deleteEdges
(
    const triSurface& surf,
    const plane& cutPlane,
    List<surfaceFeatures::edgeStatus>& edgeStat
)
{
    const pointField& points = surf.points();
    const labelList& meshPoints = surf.meshPoints();

    forAll(edgeStat, edgei)
    {
        const edge& e = surf.edges()[edgei];
        const point& p0 = points[meshPoints[e.start()]];
        const point& p1 = points[meshPoints[e.end()]];
        const linePointRef line(p0, p1);

        // If edge does not intersect the plane, delete.
        scalar intersect = cutPlane.lineIntersect(line);

        point featPoint = intersect * (p1 - p0) + p0;

        if (!line.insideBoundBox(featPoint))
        {
            edgeStat[edgei] = surfaceFeatures::NONE;
        }
    }
}


Foam::surfaceFeatures::edgeStatus Foam::checkNonManifoldEdge
(
    const triSurface& surf,
    const scalar tol,
    const scalar includedAngle,
    const label edgei
)
{
    const edge& e = surf.edges()[edgei];
    const labelList& eFaces = surf.edgeFaces()[edgei];

    // Bin according to normal

    DynamicList<Foam::vector> normals(2);
    DynamicList<labelList> bins(2);

    forAll(eFaces, eFacei)
    {
        const Foam::vector& n = surf.faceNormals()[eFaces[eFacei]];

        // Find the normal in normals
        label index = -1;
        forAll(normals, normalI)
        {
            if (mag(n&normals[normalI]) > (1-tol))
            {
                index = normalI;
                break;
            }
        }

        if (index != -1)
        {
            bins[index].append(eFacei);
        }
        else if (normals.size() >= 2)
        {
            // Would be third normal. Mark as feature.
            //Pout<< "** at edge:" << surf.localPoints()[e[0]]
            //    << surf.localPoints()[e[1]]
            //    << " have normals:" << normals
            //    << " and " << n << endl;
            return surfaceFeatures::REGION;
        }
        else
        {
            normals.append(n);
            bins.append(labelList(1, eFacei));
        }
    }


    // Check resulting number of bins
    if (bins.size() == 1)
    {
        // Note: should check here whether they are two sets of faces
        // that are planar or indeed 4 faces al coming together at an edge.
        //Pout<< "** at edge:"
        //    << surf.localPoints()[e[0]]
        //    << surf.localPoints()[e[1]]
        //    << " have single normal:" << normals[0]
        //    << endl;
        return surfaceFeatures::NONE;
    }
    else
    {
        // Two bins. Check if normals make an angle

        //Pout<< "** at edge:"
        //    << surf.localPoints()[e[0]]
        //    << surf.localPoints()[e[1]] << nl
        //    << "    normals:" << normals << nl
        //    << "    bins   :" << bins << nl
        //    << endl;

        if (includedAngle >= 0)
        {
            scalar minCos = Foam::cos(degToRad(180.0 - includedAngle));

            forAll(eFaces, i)
            {
                const Foam::vector& ni = surf.faceNormals()[eFaces[i]];
                for (label j=i+1; j<eFaces.size(); j++)
                {
                    const Foam::vector& nj = surf.faceNormals()[eFaces[j]];
                    if (mag(ni & nj) < minCos)
                    {
                        //Pout<< "have sharp feature between normal:" << ni
                        //    << " and " << nj << endl;

                        // Is feature. Keep as region or convert to
                        // feature angle? For now keep as region.
                        return surfaceFeatures::REGION;
                    }
                }
            }
        }


        // So now we have two normals bins but need to make sure both
        // bins have the same regions in it.

         // 1. store + or - region number depending
        //    on orientation of triangle in bins[0]
        const labelList& bin0 = bins[0];
        labelList regionAndNormal(bin0.size());
        forAll(bin0, i)
        {
            const labelledTri& t = surf.localFaces()[eFaces[bin0[i]]];
            int dir = t.edgeDirection(e);

            if (dir > 0)
            {
                regionAndNormal[i] = t.region()+1;
            }
            else if (dir == 0)
            {
                FatalErrorInFunction
                    << exit(FatalError);
            }
            else
            {
                regionAndNormal[i] = -(t.region()+1);
            }
        }

        // 2. check against bin1
        const labelList& bin1 = bins[1];
        labelList regionAndNormal1(bin1.size());
        forAll(bin1, i)
        {
            const labelledTri& t = surf.localFaces()[eFaces[bin1[i]]];
            int dir = t.edgeDirection(e);

            label myRegionAndNormal;
            if (dir > 0)
            {
                myRegionAndNormal = t.region()+1;
            }
            else
            {
                myRegionAndNormal = -(t.region()+1);
            }

            regionAndNormal1[i] = myRegionAndNormal;

            label index = findIndex(regionAndNormal, -myRegionAndNormal);
            if (index == -1)
            {
                // Not found.
                //Pout<< "cannot find region " << myRegionAndNormal
                //    << " in regions " << regionAndNormal << endl;

                return surfaceFeatures::REGION;
            }
        }

        return surfaceFeatures::NONE;
    }
}


void Foam::deleteNonManifoldEdges
(
    const triSurface& surf,
    const scalar tol,
    const scalar includedAngle,
    List<surfaceFeatures::edgeStatus>& edgeStat
)
{
    forAll(edgeStat, edgei)
    {
        const labelList& eFaces = surf.edgeFaces()[edgei];

        if
        (
            eFaces.size() > 2
            && edgeStat[edgei] == surfaceFeatures::REGION
            && (eFaces.size() % 2) == 0
        )
        {
            edgeStat[edgei] = checkNonManifoldEdge
            (
                surf,
                tol,
                includedAngle,
                edgei
            );
        }
    }
}


void Foam::writeStats(const extendedFeatureEdgeMesh& fem, Ostream& os)
{
    os  << "    points : " << fem.points().size() << nl
        << "    of which" << nl
        << "        convex             : "
        << fem.concaveStart() << nl
        << "        concave            : "
        << (fem.mixedStart() - fem.concaveStart()) << nl
        << "        mixed              : "
        << (fem.nonFeatureStart() - fem.mixedStart()) << nl
        << "        non-feature        : "
        << (fem.points().size() - fem.nonFeatureStart()) << nl
        << "    edges  : " << fem.edges().size() << nl
        << "    of which" << nl
        << "        external edges     : "
        << fem.internalStart() << nl
        << "        internal edges     : "
        << (fem.flatStart() - fem.internalStart()) << nl
        << "        flat edges         : "
        << (fem.openStart() - fem.flatStart()) << nl
        << "        open edges         : "
        << (fem.multipleStart() - fem.openStart()) << nl
        << "        multiply connected : "
        << (fem.edges().size() - fem.multipleStart()) << endl;
}


// ************************************************************************* //
