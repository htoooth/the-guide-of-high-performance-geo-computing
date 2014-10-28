/******************************************************************************
 * $Id: gdal_rasterize.cpp 21298 2010-12-20 10:58:34Z rouault $
 *
 * Project:  GDAL Utilities
 * Purpose:  Rasterize OGR shapes into a GDAL raster.
 * Author:   Frank Warmerdam <warmerdam@pobox.com>
 *
 ******************************************************************************
 * Copyright (c) 2005, Frank Warmerdam <warmerdam@pobox.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/
#include <vector>
#include "math.h"
#include <fstream>
#include <stdio.h>
#include <string>

#include "gt_geometry.h"
#include "gt_spatialindex.h"
#include "gt_datasource.h"
#include "gt_datadriver.h"

#include "gdal_alg_priv.h"

//#include "gdal.h"
//#include "gdal_alg.h"
//#include <gdal_priv.h>
//#include <ogr_feature.h>
//#include "cpl_conv.h"
//#include "ogr_api.h"
//#include "ogr_srs_api.h"
//#include "cpl_string.h"
//=========gdalrasterize===========
//#include "gdal_alg.h"

//#include "gdal_priv.h"
//#include "ogr_api.h"
//#include "ogr_geometry.h"
//#include "ogr_spatialref.h"

#ifdef OGR_ENABLED
#include "ogrsf_frmts.h"
#endif
//===============================
#include "mpi.h"

//=========================================

using namespace std;
using namespace gts;

CPL_CVSID("$$Id: hpgc_rasterize.cpp 21298 2011-09-26 11:13:34Z rouault $");
CPLErr hpgc_RasterizeGeometries( GDALDatasetH hDS,
                                int nBandCount, int *panBandList,
                                int nGeomCount, OGRGeometryH *pahGeometries,
                                double *padfGeomBurnValue,
								int cp, int np);
//========================================================================================
//========================================================================================
//========================================================================================
//
//gdalrasterize.cpp
//========================================================================================
//========================================================================================
//========================================================================================
/************************************************************************/
/*                           gvBurnScanline()                           */
/************************************************************************/

void gvBAF( void *pCBData, int nY, int nXStart, int nXEnd,double dfVariant )
{

}
void gvBurnScanline( void *pCBData, int nY, int nXStart, int nXEnd,double dfVariant )
{
    GDALRasterizeInfo *psInfo = (GDALRasterizeInfo *) pCBData;
    int iBand;

    if( nXStart > nXEnd )
        return;

    CPLAssert( nY >= 0 && nY < psInfo->nYSize );
    CPLAssert( nXStart <= nXEnd );
    CPLAssert( nXStart < psInfo->nXSize );
    CPLAssert( nXEnd >= 0 );

    if( nXStart < 0 )
        nXStart = 0;
    if( nXEnd >= psInfo->nXSize )
        nXEnd = psInfo->nXSize - 1;

    if( psInfo->eType == GDT_Byte )
    {
        for( iBand = 0; iBand < psInfo->nBands; iBand++ )
        {
            unsigned char *pabyInsert;
            unsigned char nBurnValue = (unsigned char)
                ( psInfo->padfBurnValue[iBand] +
                  ( (psInfo->eBurnValueSource == GBV_UserBurnValue)?
                             0 : dfVariant ) );

            pabyInsert = psInfo->pabyChunkBuf
                + iBand * psInfo->nXSize * psInfo->nYSize
                + nY * psInfo->nXSize + nXStart;

            memset( pabyInsert, nBurnValue, nXEnd - nXStart + 1 );
        }
    }
    else
    {
        for( iBand = 0; iBand < psInfo->nBands; iBand++ )
        {
            int	nPixels = nXEnd - nXStart + 1;
            float   *pafInsert;
            float   fBurnValue = (float)
                ( psInfo->padfBurnValue[iBand] +
                  ( (psInfo->eBurnValueSource == GBV_UserBurnValue)?
                             0 : dfVariant ) );

            pafInsert = ((float *) psInfo->pabyChunkBuf)
                + iBand * psInfo->nXSize * psInfo->nYSize
                + nY * psInfo->nXSize + nXStart;

            while( nPixels-- > 0 )
                *(pafInsert++) = fBurnValue;
        }
    }
}

/************************************************************************/
/*                            gvBurnPoint()                             */
/************************************************************************/

void gvBurnPoint( void *pCBData, int nY, int nX, double dfVariant )

{
    GDALRasterizeInfo *psInfo = (GDALRasterizeInfo *) pCBData;
    int iBand;

    CPLAssert( nY >= 0 && nY < psInfo->nYSize );
    CPLAssert( nX >= 0 && nX < psInfo->nXSize );

    if( psInfo->eType == GDT_Byte )
    {
        for( iBand = 0; iBand < psInfo->nBands; iBand++ )
        {
			//移动指针，获得对应（nX,nY）的栅格点的指针
            unsigned char *pbyInsert = psInfo->pabyChunkBuf
                                      + iBand * psInfo->nXSize * psInfo->nYSize
                                      + nY * psInfo->nXSize + nX;

			//修改栅格点栅格值
            *pbyInsert = (unsigned char)( psInfo->padfBurnValue[iBand] +
                          ( (psInfo->eBurnValueSource == GBV_UserBurnValue)? 0 : dfVariant ) );
        }
    }
    else
    {
        for( iBand = 0; iBand < psInfo->nBands; iBand++ )
        {
            float   *pfInsert = ((float *) psInfo->pabyChunkBuf)
                                + iBand * psInfo->nXSize * psInfo->nYSize
                                + nY * psInfo->nXSize + nX;

            *pfInsert = (float)( psInfo->padfBurnValue[iBand] +
                         ( (psInfo->eBurnValueSource == GBV_UserBurnValue)? 0 : dfVariant ) );
        }
    }
}

/************************************************************************/
/*                    GDALCollectRingsFromGeometry()                    */
/************************************************************************/

static void GDALCollectRingsFromGeometry(
    OGRGeometry *poShape,
    std::vector<double> &aPointX, 
	std::vector<double> &aPointY,
    std::vector<double> &aPointVariant,
    std::vector<int> &aPartSize, 
	GDALBurnValueSrc eBurnValueSrc)

{
    if( poShape == NULL )
        return;

    OGRwkbGeometryType eFlatType = wkbFlatten(poShape->getGeometryType());
    int i;

    if ( eFlatType == wkbPoint )
    {
        OGRPoint    *poPoint = (OGRPoint *) poShape;
        int nNewCount = aPointX.size() + 1;

        aPointX.reserve( nNewCount );
        aPointY.reserve( nNewCount );
        aPointX.push_back( poPoint->getX() );
        aPointY.push_back( poPoint->getY() );
        aPartSize.push_back( 1 );
        if( eBurnValueSrc != GBV_UserBurnValue )
        {
            /*switc(h eBurnValueSrc )
            {
            case GBV_Z:*/
                aPointVariant.reserve( nNewCount );
                aPointVariant.push_back( poPoint->getZ() );
                /*break;
            case GBV_M:
                aPointVariant.reserve( nNewCount );
                aPointVariant.push_back( poPoint->getM() );
            }*/
        }
    }
    else if ( eFlatType == wkbLineString )
    {
        OGRLineString   *poLine = (OGRLineString *) poShape;
        int nCount = poLine->getNumPoints();
        int nNewCount = aPointX.size() + nCount;

        aPointX.reserve( nNewCount );
        aPointY.reserve( nNewCount );
        if( eBurnValueSrc != GBV_UserBurnValue )
            aPointVariant.reserve( nNewCount );
        for ( i = nCount - 1; i >= 0; i-- )
        {
            aPointX.push_back( poLine->getX(i) );
            aPointY.push_back( poLine->getY(i) );
            if( eBurnValueSrc != GBV_UserBurnValue )
            {
                /*switch( eBurnValueSrc )
                {
                    case GBV_Z:*/
                        aPointVariant.push_back( poLine->getZ(i) );
                        /*break;
                    case GBV_M:
                        aPointVariant.push_back( poLine->getM(i) );
                }*/
            }
        }
        aPartSize.push_back( nCount );
    }
    else if ( EQUAL(poShape->getGeometryName(),"LINEARRING") )
    {
        OGRLinearRing *poRing = (OGRLinearRing *) poShape;
        int nCount = poRing->getNumPoints();
        int nNewCount = aPointX.size() + nCount;

        aPointX.reserve( nNewCount );
        aPointY.reserve( nNewCount );
        if( eBurnValueSrc != GBV_UserBurnValue )
            aPointVariant.reserve( nNewCount );
        for ( i = nCount - 1; i >= 0; i-- )
        {
            aPointX.push_back( poRing->getX(i) );
            aPointY.push_back( poRing->getY(i) );
        }
        if( eBurnValueSrc != GBV_UserBurnValue )
        {
            /*switch( eBurnValueSrc )
            {
            case GBV_Z:*/
                aPointVariant.push_back( poRing->getZ(i) );
                /*break;
            case GBV_M:
                aPointVariant.push_back( poRing->getM(i) );
            }*/
        }
        aPartSize.push_back( nCount );
    }
    else if( eFlatType == wkbPolygon )
    {
        OGRPolygon *poPolygon = (OGRPolygon *) poShape;

        GDALCollectRingsFromGeometry( poPolygon->getExteriorRing(),
                                      aPointX, aPointY, aPointVariant,
                                      aPartSize, eBurnValueSrc );

        for( i = 0; i < poPolygon->getNumInteriorRings(); i++ )
            GDALCollectRingsFromGeometry( poPolygon->getInteriorRing(i),
                                          aPointX, aPointY, aPointVariant,
                                          aPartSize, eBurnValueSrc );
    }

    else if( eFlatType == wkbMultiPoint
             || eFlatType == wkbMultiLineString
             || eFlatType == wkbMultiPolygon
             || eFlatType == wkbGeometryCollection )
    {
        OGRGeometryCollection *poGC = (OGRGeometryCollection *) poShape;

        for( i = 0; i < poGC->getNumGeometries(); i++ )
            GDALCollectRingsFromGeometry( poGC->getGeometryRef(i),
                                          aPointX, aPointY, aPointVariant,
                                          aPartSize, eBurnValueSrc );
    }
    else
    {
        CPLDebug( "GDAL", "Rasterizer ignoring non-polygonal geometry." );
    }
}

/************************************************************************/
/*                       gv_rasterize_one_shape()                       */
/************************************************************************/
static void
gv_rasterize_one_shape( unsigned char *pabyChunkBuf, 
					    int nYOff,
                        int nXSize, int nYSize,
                        int nBands, GDALDataType eType, int bAllTouched,
                        OGRGeometry *poShape, double *padfBurnValue,
                        GDALBurnValueSrc eBurnValueSrc,
                        GDALTransformerFunc pfnTransformer,
                        void *pTransformArg )

{
    GDALRasterizeInfo sInfo;

    if (poShape == NULL)
        return;

    sInfo.nXSize = nXSize;
    sInfo.nYSize = nYSize;
    sInfo.nBands = nBands;
    sInfo.pabyChunkBuf = pabyChunkBuf;
    sInfo.eType = eType;
    sInfo.padfBurnValue = padfBurnValue;
    sInfo.eBurnValueSource = eBurnValueSrc;

/* -------------------------------------------------------------------- */
/*      Transform polygon geometries into a set of rings and a part     */
/*      size list.                                                      */
/* -------------------------------------------------------------------- */
    std::vector<double> aPointX;
    std::vector<double> aPointY;
    std::vector<double> aPointVariant;
    std::vector<int> aPartSize;

    GDALCollectRingsFromGeometry( poShape, aPointX, aPointY, aPointVariant,
                                    aPartSize, eBurnValueSrc );

/* -------------------------------------------------------------------- */
/*      Transform points if needed.                                     */
/* -------------------------------------------------------------------- */
    if( pfnTransformer != NULL )
    {
        int *panSuccess = (int *) CPLCalloc(sizeof(int),aPointX.size());

        // TODO: we need to add all appropriate error checking at some point.
        pfnTransformer( pTransformArg, FALSE, aPointX.size(),
                        &(aPointX[0]), &(aPointY[0]), NULL, panSuccess );
        CPLFree( panSuccess );
    }

/* -------------------------------------------------------------------- */
/*      Shift to account for the buffer offset of this buffer.          */
/* -------------------------------------------------------------------- */
    unsigned int i;

    for( i = 0; i < aPointY.size(); i++ )
        aPointY[i] -= nYOff;

/* -------------------------------------------------------------------- */
/*      Perform the rasterization.                                      */
/*      According to the C++ Standard/23.2.4, elements of a vector are  */
/*      stored in continuous memory block.                              */
/* -------------------------------------------------------------------- */

    // TODO - mloskot: Check if vectors are empty, otherwise it may
    // lead to undefined behavior by returning non-referencable pointer.
    // if (!aPointX.empty())
    //    /* fill polygon */
    // else
    //    /* How to report this problem? */
    switch ( wkbFlatten(poShape->getGeometryType()) )
    {
        case wkbPoint:
        case wkbMultiPoint:
            GDALdllImagePoint( sInfo.nXSize, nYSize,
                               aPartSize.size(), &(aPartSize[0]),
                               &(aPointX[0]), &(aPointY[0]),
                               (eBurnValueSrc == GBV_UserBurnValue)?
                                   NULL : &(aPointVariant[0]),
                               gvBurnPoint, &sInfo );
            break;
        case wkbLineString:
        case wkbMultiLineString:
        {
                GDALdllImageLine( sInfo.nXSize, nYSize,
                                  aPartSize.size(), &(aPartSize[0]),
                                  &(aPointX[0]), &(aPointY[0]),
                                  (eBurnValueSrc == GBV_UserBurnValue)?
                                      NULL : &(aPointVariant[0]),
                                  gvBurnPoint, &sInfo );
        }
        break;

        default:
        {
            GDALdllImageFilledPolygon( sInfo.nXSize, nYSize,
                                       aPartSize.size(), &(aPartSize[0]),
                                       &(aPointX[0]), &(aPointY[0]),
                                       (eBurnValueSrc == GBV_UserBurnValue)?
                                           NULL : &(aPointVariant[0]),
                                       gvBurnScanline, &sInfo );
            if( bAllTouched )
            {
                /* Reverting the variants to the first value because the
                   polygon is filled using the variant from the first point of
                   the first segment. Should be removed when the code to full
                   polygons more appropriately is added. */
                if(eBurnValueSrc == GBV_UserBurnValue)
                {
                GDALdllImageLineAllTouched( sInfo.nXSize, nYSize,
                                            aPartSize.size(), &(aPartSize[0]),
                                            &(aPointX[0]), &(aPointY[0]),
                                            NULL,
                                            gvBurnPoint, &sInfo );
                }
                else
                {
                    unsigned int n;
                    for ( i = 0, n = 0; i < aPartSize.size(); i++ )
                    {
                        int j;
                        for ( j = 0; j < aPartSize[i]; j++ )
                            aPointVariant[n++] = aPointVariant[0];
                    }

                    GDALdllImageLineAllTouched( sInfo.nXSize, nYSize,
                                                aPartSize.size(), &(aPartSize[0]),
                                                &(aPointX[0]), &(aPointY[0]),
                                                &(aPointVariant[0]),
                                                gvBurnPoint, &sInfo );
                }
            }
        }
        break;
    }
}

/************************************************************************/
/*                      GDALRasterizeGeometries()                       */
/************************************************************************/

/**
 * Burn geometries into raster.
 *
 * Rasterize a list of geometric objects into a raster dataset.  The
 * geometries are passed as an array of OGRGeometryH handlers.
 *
 * If the geometries are in the georferenced coordinates of the raster
 * dataset, then the pfnTransform may be passed in NULL and one will be
 * derived internally from the geotransform of the dataset.  The transform
 * needs to transform the geometry locations into pixel/line coordinates
 * on the raster dataset.
 *
 * The output raster may be of any GDAL supported datatype, though currently
 * internally the burning is done either as GDT_Byte or GDT_Float32.  This
 * may be improved in the future.  An explicit list of burn values for
 * each geometry for each band must be passed in.
 *
 * The papszOption list of options currently only supports one option. The
 * "ALL_TOUCHED" option may be enabled by setting it to "TRUE".
 *
 * @param hDS output data, must be opened in update mode.
 * @param nBandCount the number of bands to be updated.
 * @param panBandList the list of bands to be updated.
 * @param nGeomCount the number of geometries being passed in pahGeometries.
 * @param pahGeometries the array of geometries to burn in.
 * @param pfnTransformer transformation to apply to geometries to put into
 * pixel/line coordinates on raster.  If NULL a geotransform based one will
 * be created internally.
 * @param pTransformerArg callback data for transformer.
 * @param padfGeomBurnValue the array of values to burn into the raster.
 * There should be nBandCount values for each geometry.
 * @param papszOptions special options controlling rasterization
 * <dl>
 * <dt>"ALL_TOUCHED":</dt> <dd>May be set to TRUE to set all pixels touched
 * by the line or polygons, not just those whose center is within the polygon
 * or that are selected by brezenhams line algorithm.  Defaults to FALSE.</dd>
 * <dt>"BURN_VALUE_FROM":</dt> <dd>May be set to "Z" to use the Z values of the
 * geometries. dfBurnValue is added to this before burning.
 * Defaults to GDALBurnValueSrc.GBV_UserBurnValue in which case just the
 * dfBurnValue is burned. This is implemented only for points and lines for
 * now. The M value may be supported in the future.</dd>
 * </dl>
 * @param pfnProgress the progress function to report completion.
 * @param pProgressArg callback data for progress function.
 *
 * @return CE_None on success or CE_Failure on error.
 */

CPLErr hpgc_RasterizeGeometries( GDALDatasetH hDS,
                                int nBandCount, int *panBandList,
                                int nGeomCount, OGRGeometryH *pahGeometries,
                                double *padfGeomBurnValue,
								int cp, int np)

{
    GDALDataType   eType;
    int            nYChunkSize, nScanlineBytes;
    unsigned char *pabyChunkBuf;
    int            iY;
    GDALDataset *poDS = (GDALDataset *) hDS;

/* -------------------------------------------------------------------- */
/*      Do some rudimentary arg checking.                               */
/* -------------------------------------------------------------------- */
    if( nBandCount == 0 || nGeomCount == 0 )
        return CE_None;

    // prototype band.
    GDALRasterBand *poBand = poDS->GetRasterBand( panBandList[0] );
    if (poBand == NULL)
        return CE_Failure;

    int bAllTouched =  FALSE ;
    GDALBurnValueSrc eBurnValueSource = GBV_UserBurnValue;
    
/* -------------------------------------------------------------------- */
/*      If we have no transformer, assume the geometries are in file    */
/*      georeferenced coordinates, and create a transformer to          */
/*      convert that to pixel/line coordinates.                         */
/*                                                                      */
/*      We really just need to apply an affine transform, but for       */
/*      simplicity we use the more general GenImgProjTransformer.       */
/* -------------------------------------------------------------------- */
    GDALTransformerFunc pfnTransformer;
    void *pTransformArg;

    pTransformArg =GDALCreateGenImgProjTransformer( NULL, NULL, hDS, NULL,FALSE, 0.0, 0);
    pfnTransformer = GDALGenImgProjTransform;

/* -------------------------------------------------------------------- */
/*      Establish a chunksize to operate on.  The larger the chunk      */
/*      size the less times we need to make a pass through all the      */
/*      shapes.                                                         */
/* -------------------------------------------------------------------- */
    if( poBand->GetRasterDataType() == GDT_Byte )
        eType = GDT_Byte;
    else
        eType = GDT_Float32;

	//the total bytes needed to store one raster line 
    nScanlineBytes = nBandCount * poDS->GetRasterXSize() * (GDALGetDataTypeSize(eType)/8);

	//raster chunk size for each process
    nYChunkSize =(int) ceil(poDS->GetRasterYSize()/(double)np);
	//the very start point of each process
	iY=cp*nYChunkSize;

    int	nThisYChunkSize;
    int iShape;

    nThisYChunkSize = nYChunkSize;
    if( nThisYChunkSize + iY > poDS->GetRasterYSize() )
        nThisYChunkSize = poDS->GetRasterYSize() - iY;

    pabyChunkBuf = (unsigned char *) VSIMalloc(nThisYChunkSize * nScanlineBytes);
    if( pabyChunkBuf == NULL )
    {
        CPLError( CE_Failure, CPLE_OutOfMemory,
                  "Unable to allocate rasterization buffer." );
        return CE_Failure;
    }

    CPLErr  eErr = CE_None;

    eErr =poDS->RasterIO(GF_Read,
                       0, iY, poDS->GetRasterXSize(), nThisYChunkSize,
                       pabyChunkBuf,poDS->GetRasterXSize(),nThisYChunkSize,
                       eType, nBandCount, panBandList,
                       0, 0, 0 );
    if( eErr != CE_None )
	{
		VSIFree( pabyChunkBuf );
		return eErr;
	}

    for( iShape = 0; iShape < nGeomCount; iShape++ )
    {
        gv_rasterize_one_shape( pabyChunkBuf, iY,
                                poDS->GetRasterXSize(), nThisYChunkSize,
                                nBandCount, eType, bAllTouched,
                                (OGRGeometry *) pahGeometries[iShape],
                                padfGeomBurnValue + iShape*nBandCount,
                                eBurnValueSource,
                                pfnTransformer, pTransformArg );
    }

    eErr =poDS->RasterIO( GF_Write, 0, iY,
                        poDS->GetRasterXSize(), nThisYChunkSize,
                        pabyChunkBuf,
                        poDS->GetRasterXSize(), nThisYChunkSize,
                        eType, nBandCount, panBandList, 0, 0, 0 );
/* -------------------------------------------------------------------- */
/*      cleanup                                                         */
/* -------------------------------------------------------------------- */
    VSIFree( pabyChunkBuf );
    GDALDestroyTransformer( pTransformArg );
    return eErr;
}



//========================================================================================
//========================================================================================
//========================================================================================
//
//gdalrasterize.cpp
//========================================================================================
//========================================================================================
//========================================================================================



//========================================================================================
//========================================================================================
//========================================================================================
//****************************************************************************************
//llrasterize.cpp
//========================================================================================
//========================================================================================
//========================================================================================
static int llCompareInt(const void *a, const void *b)
{
	return (*(const int *)a) - (*(const int *)b);
}

static void llSwapDouble(double *a, double *b)
{
	double temp = *a;
	*a = *b;
	*b = temp;
}

/************************************************************************/
/*                       dllImageFilledPolygon()                        */
/*                                                                      */
/*      Perform scanline conversion of the passed multi-ring            */
/*      polygon.  Note the polygon does not need to be explicitly       */
/*      closed.  The scanline function will be called with              */
/*      horizontal scanline chunks which may not be entirely            */
/*      contained within the valid raster area (in the X                */
/*      direction).                                                     */
/*                                                                      */
/*      NEW: Nodes' coordinate are kept as double  in order             */
/*      to compute accurately the intersections with the lines          */
/*                                                                      */
/*      Two methods for determining the border pixels:                  */
/*                                                                      */
/*      1) method = 0                                                   */
/*         Inherits algorithm from version above but with several bugs  */
/*         fixed except for the cone facing down.                       */
/*         A pixel on which a line intersects a segment of a            */
/*         polygon will always be considered as inside the shape.       */
/*         Note that we only compute intersections with lines that      */
/*         passes through the middle of a pixel (line coord = 0.5,      */
/*         1.5, 2.5, etc.)                                              */
/*                                                                      */
/*      2) method = 1:                                                  */
/*         A pixel is considered inside a polygon if its center         */
/*         falls inside the polygon. This is somehow more robust unless */
/*         the nodes are placed in the center of the pixels in which    */
/*         case, due to numerical inaccuracies, it's hard to predict    */
/*         if the pixel will be considered inside or outside the shape. */
/************************************************************************/

/*
 * NOTE: This code was originally adapted from the gdImageFilledPolygon()
 * function in libgd.
 *
 * http://www.boutell.com/gd/
 *
 * It was later adapted for direct inclusion in GDAL and relicensed under
 * the GDAL MIT/X license (pulled from the OpenEV distribution).
 */

void GDALdllImageFilledPolygon(int nRasterXSize, int nRasterYSize,
                               int nPartCount, int *panPartSize,
                               double *padfX, double *padfY,
                               double *dfVariant,
                               llScanlineFunc pfnScanlineFunc, void *pCBData )
{
/*************************************************************************
2nd Method (method=1):
=====================
No known bug
*************************************************************************/

    int i;
    int y;
    int miny, maxy,minx,maxx;
    double dminy, dmaxy;
    double dx1, dy1;
    double dx2, dy2;
    double dy;
    double intersect;


    int ind1, ind2;
    int ints, n, part;
    int *polyInts, polyAllocated;


    int horizontal_x1, horizontal_x2;


    if (!nPartCount) {
        return;
    }

    n = 0;
    for( part = 0; part < nPartCount; part++ )
        n += panPartSize[part];

    polyInts = (int *) malloc(sizeof(int) * n);
    polyAllocated = n;

    dminy = padfY[0];
    dmaxy = padfY[0];
    for (i=1; (i < n); i++) {

        if (padfY[i] < dminy) {
            dminy = padfY[i];
        }
        if (padfY[i] > dmaxy) {
            dmaxy = padfY[i];
        }
    }
    miny = (int) dminy;
    maxy = (int) dmaxy;


    if( miny < 0 )
        miny = 0;
    if( maxy >= nRasterYSize )
        maxy = nRasterYSize-1;


    minx = 0;
    maxx = nRasterXSize - 1;

    /* Fix in 1.3: count a vertex only once */
    for (y=miny; y <= maxy; y++) {
        int	partoffset = 0;

        dy = y +0.5; /* center height of line*/


        part = 0;
        ints = 0;

        /*Initialize polyInts, otherwise it can sometimes causes a seg fault */
        memset(polyInts, -1, sizeof(int) * n);

        for (i=0; (i < n); i++) {


            if( i == partoffset + panPartSize[part] ) {
                partoffset += panPartSize[part];
                part++;
            }

            if( i == partoffset ) {
                ind1 = partoffset + panPartSize[part] - 1;
                ind2 = partoffset;
            } else {
                ind1 = i-1;
                ind2 = i;
            }


            dy1 = padfY[ind1];
            dy2 = padfY[ind2];


            if( (dy1 < dy && dy2 < dy) || (dy1 > dy && dy2 > dy) )
                continue;

            if (dy1 < dy2) {
                dx1 = padfX[ind1];
                dx2 = padfX[ind2];
            } else if (dy1 > dy2) {
                dy2 = padfY[ind1];
                dy1 = padfY[ind2];
                dx2 = padfX[ind1];
                dx1 = padfX[ind2];
            } else /* if (fabs(dy1-dy2)< 1.e-6) */
	    {

                /*AE: DO NOT skip bottom horizontal segments
		  -Fill them separately-
		  They are not taken into account twice.*/
		if (padfX[ind1] > padfX[ind2])
		{
		    horizontal_x1 = (int) floor(padfX[ind2]+0.5);
		    horizontal_x2 = (int) floor(padfX[ind1]+0.5);

                    if  ( (horizontal_x1 >  maxx) ||  (horizontal_x2 <= minx) )
                        continue;

		    /*fill the horizontal segment (separately from the rest)*/
		    pfnScanlineFunc( pCBData, y, horizontal_x1, horizontal_x2 - 1, (dfVariant == NULL)?0:dfVariant[0] );
		    continue;
		}
		else /*skip top horizontal segments (they are already filled in the regular loop)*/
		    continue;

	    }

            if(( dy < dy2 ) && (dy >= dy1))
            {

                intersect = (dy-dy1) * (dx2-dx1) / (dy2-dy1) + dx1;

		polyInts[ints++] = (int) floor(intersect+0.5);
	    }
	}
        /*
         * It would be more efficient to do this inline, to avoid
         * a function call for each comparison.
	 * NOTE - mloskot: make llCompareInt a functor and use std
	 * algorithm and it will be optimized and expanded
	 * automatically in compile-time, with modularity preserved.
         */
        qsort(polyInts, ints, sizeof(int), llCompareInt);


        for (i=0; (i < (ints)); i+=2)
        {
            if( polyInts[i] <= maxx && polyInts[i+1] > minx )
            {
                pfnScanlineFunc( pCBData, y, polyInts[i], polyInts[i+1] - 1, (dfVariant == NULL)?0:dfVariant[0] );
            }
        }
    }

    free( polyInts );
}

/************************************************************************/
/*                         GDALdllImagePoint()                          */
/************************************************************************/

void GDALdllImagePoint( int nRasterXSize, int nRasterYSize,
                        int nPartCount, int *panPartSize,
                        double *padfX, double *padfY, double *padfVariant,
                        llPointFunc pfnPointFunc, void *pCBData )
{
    int     i;

    for ( i = 0; i < nPartCount; i++ )
    {
        int nX = (int)floor( padfX[i] + 0.5 );
        int nY = (int)floor( padfY[i] + 0.5 );
        double dfVariant = 0;
        if( padfVariant != NULL )
            dfVariant = padfVariant[i];

        if ( 0 <= nX && nX < nRasterXSize && 0 <= nY && nY < nRasterYSize )
            pfnPointFunc( pCBData, nY, nX, dfVariant );
    }
}

/************************************************************************/
/*                         GDALdllImageLine()                           */
/************************************************************************/

void GDALdllImageLine( int nRasterXSize, int nRasterYSize,
                       int nPartCount, int *panPartSize,
                       double *padfX, double *padfY, double *padfVariant,
                       llPointFunc pfnPointFunc, void *pCBData )
{
    int     i, n;

    if ( !nPartCount )
        return;

    for ( i = 0, n = 0; i < nPartCount; n += panPartSize[i++] )
    {
        int j;

        for ( j = 1; j < panPartSize[i]; j++ )
        {
            int iX = (int)floor( padfX[n + j - 1] + 0.5 );
            int iY = (int)floor( padfY[n + j - 1] + 0.5 );

            const int iX1 = (int)floor( padfX[n + j] + 0.5 );
            const int iY1 = (int)floor( padfY[n + j] + 0.5 );

            double dfVariant = 0, dfVariant1 = 0;
            if( padfVariant != NULL &&
                ((GDALRasterizeInfo *)pCBData)->eBurnValueSource !=
                    GBV_UserBurnValue )
            {
                dfVariant = padfVariant[n + j - 1];
                dfVariant1 = padfVariant[n + j];
            }

            int nDeltaX = ABS( iX1 - iX );
            int nDeltaY = ABS( iY1 - iY );

            // Step direction depends on line direction.
            const int nXStep = ( iX > iX1 ) ? -1 : 1;
            const int nYStep = ( iY > iY1 ) ? -1 : 1;

            // Determine the line slope.
            if ( nDeltaX >= nDeltaY )
            {
                const int nXError = nDeltaY << 1;
                const int nYError = nXError - (nDeltaX << 1);
                int nError = nXError - nDeltaX;
                double dfDeltaVariant = (dfVariant1 - dfVariant) /
                                                           (double)nDeltaX;

                while ( nDeltaX-- >= 0 )
                {
                    if ( 0 <= iX && iX < nRasterXSize
                         && 0 <= iY && iY < nRasterYSize )
                        pfnPointFunc( pCBData, iY, iX, dfVariant );

                    dfVariant += dfDeltaVariant;
                    iX += nXStep;
                    if ( nError > 0 )
                    {
                        iY += nYStep;
                        nError += nYError;
                    }
                    else
                        nError += nXError;
                }
            }
            else
            {
                const int nXError = nDeltaX << 1;
                const int nYError = nXError - (nDeltaY << 1);
                int nError = nXError - nDeltaY;
                double dfDeltaVariant = (dfVariant1 - dfVariant) /
                                                           (double)nDeltaY;

                while ( nDeltaY-- >= 0 )
                {
                    if ( 0 <= iX && iX < nRasterXSize
                         && 0 <= iY && iY < nRasterYSize )
                        pfnPointFunc( pCBData, iY, iX, dfVariant );

                    dfVariant += dfDeltaVariant;
                    iY += nYStep;
                    if ( nError > 0 )
                    {
                        iX += nXStep;
                        nError += nYError;
                    }
                    else
                        nError += nXError;
                }
            }
        }
    }
}

/************************************************************************/
/*                     GDALdllImageLineAllTouched()                     */
/*                                                                      */
/*      This alternate line drawing algorithm attempts to ensure        */
/*      that every pixel touched at all by the line will get set.       */
/*      @param padfVariant should contain the values that are to be     */
/*      added to the burn value.  The values along the line between the */
/*      points will be linearly interpolated. These values are used     */
/*      only if pCBData->eBurnValueSource is set to something other     */
/*      than GBV_UserBurnValue. If NULL is passed, a monotonous line    */
/*      will be drawn with the burn value.                              */
/************************************************************************/

void
GDALdllImageLineAllTouched(int nRasterXSize, int nRasterYSize,
                           int nPartCount, int *panPartSize,
                           double *padfX, double *padfY, double *padfVariant,
                           llPointFunc pfnPointFunc, void *pCBData )

{
    int     i, n;

    if ( !nPartCount )
        return;

    for ( i = 0, n = 0; i < nPartCount; n += panPartSize[i++] )
    {
        int j;

        for ( j = 1; j < panPartSize[i]; j++ )
        {
            double dfX = padfX[n + j - 1];
            double dfY = padfY[n + j - 1];

            double dfXEnd = padfX[n + j];
            double dfYEnd = padfY[n + j];

            double dfVariant = 0, dfVariantEnd = 0;
            if( padfVariant != NULL &&
                ((GDALRasterizeInfo *)pCBData)->eBurnValueSource !=
                    GBV_UserBurnValue )
            {
                dfVariant = padfVariant[n + j - 1];
                dfVariantEnd = padfVariant[n + j];
            }

            // Skip segments that are off the target region.
            if( (dfY < 0 && dfYEnd < 0)
                || (dfY > nRasterYSize && dfYEnd > nRasterYSize)
                || (dfX < 0 && dfXEnd < 0)
                || (dfX > nRasterXSize && dfXEnd > nRasterXSize) )
                continue;

            // Swap if needed so we can proceed from left2right (X increasing)
            if( dfX > dfXEnd )
            {
                llSwapDouble( &dfX, &dfXEnd );
                llSwapDouble( &dfY, &dfYEnd );
                llSwapDouble( &dfVariant, &dfVariantEnd );
            }

            // Special case for vertical lines.
            if( floor(dfX) == floor(dfXEnd) )
            {
                if( dfYEnd < dfY )
                {
                    llSwapDouble( &dfY, &dfYEnd );
                    llSwapDouble( &dfVariant, &dfVariantEnd );
                }

                int iX = (int) floor(dfX);
                int iY = (int) floor(dfY);
                int iYEnd = (int) floor(dfYEnd);

                if( iX >= nRasterXSize )
                    continue;

                double dfDeltaVariant = 0;
                if(( dfYEnd - dfY ) > 0)
                    dfDeltaVariant = ( dfVariantEnd - dfVariant )
                                     / ( dfYEnd - dfY );//per unit change in iY

                // Clip to the borders of the target region
                if( iY < 0 )
                    iY = 0;
                if( iYEnd >= nRasterYSize )
                    iYEnd = nRasterYSize - 1;
                dfVariant += dfDeltaVariant * ( (double)iY - dfY );

                if( padfVariant == NULL )
                    for( ; iY <= iYEnd; iY++ )
                        pfnPointFunc( pCBData, iY, iX, 0.0 );
                else
                    for( ; iY <= iYEnd; iY++, dfVariant +=  dfDeltaVariant )
                        pfnPointFunc( pCBData, iY, iX, dfVariant );

                continue; // next segment
            }

            double dfDeltaVariant = ( dfVariantEnd - dfVariant )
                                    / ( dfXEnd - dfX );//per unit change in iX

            // special case for horizontal lines
            if( floor(dfY) == floor(dfYEnd) )
            {
                if( dfXEnd < dfX )
                {
                    llSwapDouble( &dfX, &dfXEnd );
                    llSwapDouble( &dfVariant, &dfVariantEnd );
                }

                int iX = (int) floor(dfX);
                int iY = (int) floor(dfY);
                int iXEnd = (int) floor(dfXEnd);

                if( iY >= nRasterYSize )
                    continue;

                // Clip to the borders of the target region
                if( iX < 0 )
                    iX = 0;
                if( iXEnd >= nRasterXSize )
                    iXEnd = nRasterXSize - 1;
                dfVariant += dfDeltaVariant * ( (double)iX - dfX );

                if( padfVariant == NULL )
                    for( ; iX <= iXEnd; iX++ )
                        pfnPointFunc( pCBData, iY, iX, 0.0 );
                else
                    for( ; iX <= iXEnd; iX++, dfVariant +=  dfDeltaVariant )
                        pfnPointFunc( pCBData, iY, iX, dfVariant );

                continue; // next segment
            }

/* -------------------------------------------------------------------- */
/*      General case - left to right sloped.                            */
/* -------------------------------------------------------------------- */
            double dfSlope = (dfYEnd - dfY) / (dfXEnd - dfX);

            // clip segment in X
            if( dfXEnd > nRasterXSize )
            {
                dfYEnd -= ( dfXEnd - (double)nRasterXSize ) * dfSlope;
                dfXEnd = nRasterXSize;
            }
            if( dfX < 0 )
            {
                dfY += (0 - dfX) * dfSlope;
                dfVariant += dfDeltaVariant * (0.0 - dfX);
                dfX = 0.0;
            }

            // clip segment in Y
            double dfDiffX;
            if( dfYEnd > dfY )
            {
                if( dfY < 0 )
                {
                    dfX += (dfDiffX = (0 - dfY) / dfSlope);
                    dfVariant += dfDeltaVariant * dfDiffX;
                    dfY = 0.0;
                }
                if( dfYEnd >= nRasterYSize )
                {
                    dfXEnd += ( dfYEnd - (double)nRasterYSize ) / dfSlope;
                    dfYEnd = nRasterXSize;
                }
            }
            else
            {
                if( dfY >= nRasterYSize )
                {
                    dfX += (dfDiffX = ((double)nRasterYSize - dfY) / dfSlope);
                    dfVariant += dfDeltaVariant * dfDiffX;
                    dfY = nRasterYSize;
                }
                if( dfYEnd < 0 )
                {
                    dfXEnd -= ( dfYEnd - 0 ) / dfSlope;
                    dfYEnd = 0;
                }
            }

            // step from pixel to pixel.
            while( dfX < dfXEnd )
            {
                int iX = (int) floor(dfX);
                int iY = (int) floor(dfY);

                // burn in the current point.
                // We should be able to drop the Y check because we cliped in Y,
                // but there may be some error with all the small steps.
                if( iY >= 0 && iY < nRasterYSize )
                    pfnPointFunc( pCBData, iY, iX, dfVariant );

                double dfStepX = floor(dfX+1.0) - dfX;
                double dfStepY = dfStepX * dfSlope;

                // step to right pixel without changing scanline?
                if( (int) floor(dfY + dfStepY) == iY )
                {
                    dfX += dfStepX;
                    dfY += dfStepY;
                    dfVariant += dfDeltaVariant * dfStepX;
                }
                else if( dfSlope < 0 )
                {
                    dfStepY = iY - dfY;
                    if( dfStepY > -0.000000001 )
                        dfStepY = -0.000000001;

                    dfStepX = dfStepY / dfSlope;
                    dfX += dfStepX;
                    dfY += dfStepY;
                    dfVariant += dfDeltaVariant * dfStepX;
                }
                else
                {
                    dfStepY = (iY + 1) - dfY;
                    if( dfStepY < 0.000000001 )
                        dfStepY = 0.000000001;

                    dfStepX = dfStepY / dfSlope;
                    dfX += dfStepX;
                    dfY += dfStepY;
                    dfVariant += dfDeltaVariant * dfStepX;
                }
            } // next step along sement.

        } // next segment
    } // next part
}
//========================================================================================
//========================================================================================
//========================================================================================
//****************************************************************************************
//llrasterize.cpp
//========================================================================================
//========================================================================================
//========================================================================================

void exitMPI(int code)
{
	int cp;
    char processor_name[MPI_MAX_PROCESSOR_NAME];
    int namelen;
    MPI_Get_processor_name(processor_name,&namelen);
    MPI_Comm_rank(MPI_COMM_WORLD, &cp);
    printf("Processor NO.%d on the node [%s] quitted...\n",cp,processor_name);
	MPI_Finalize();
	exit(0);
}
/************************************************************************/
/*                            ArgIsNumeric()                            */
/************************************************************************/

static int ArgIsNumeric(const char *pszArg)

{
	if (pszArg[0] == '-')
		pszArg++;

	if (*pszArg == '\0')
		return FALSE;

	while (*pszArg != '\0') {
		if ((*pszArg < '0' || *pszArg > '9') && *pszArg != '.')
			return FALSE;
		pszArg++;
	}

	return TRUE;
}

/************************************************************************/
/*                               Usage()                                */
/************************************************************************/

static void Usage()

{	printf(	"Usage: hpgc_rasterize [-b band]* [-l layername]* \n"
					"       [-burn value]* | [-a attribute_name]\n"
					"       [-of format] [-a_nodata value] [-init value]*\n"
					"       [-tr xres yres] [-ts width height]\n"
					"       [-ot {Byte/Int16/UInt16/UInt32/Int32/Float32/Float64/\n"
					"             CInt16/CInt32/CFloat32/CFloat64}] \n"
					"       <src_datasource> <dst_filename>\n");
}

/************************************************************************/
/*                            ProcessLayer()                            */
/*                                                                      */
/*      Process all the features in a layer selection, collecting       */
/*      geometries and burn values.                                     */
/************************************************************************/

static void ProcessLayer(OGRLayerH hSrcLayer,
						 GDALDatasetH hDstDS, 
						 std::vector<int> anBandList,
						 std::vector<double> &adfBurnValues,
						 const char *pszBurnAttribute, 
						 int cp, int np)
{
/* -------------------------------------------------------------------- */
/*      Get field index, and check.                                     */
/* -------------------------------------------------------------------- */
    int iBurnField = -1;

    if( pszBurnAttribute )
    {
        iBurnField = OGR_FD_GetFieldIndex( OGR_L_GetLayerDefn( hSrcLayer ),pszBurnAttribute );
        if( iBurnField == -1 )
        {
            printf( "Failed to find field %s on layer %s, skipping.\n",
                    pszBurnAttribute,
                    OGR_FD_GetName( OGR_L_GetLayerDefn( hSrcLayer ) ) );
            return;
        }
    }

/* ---------------------------------------------------------------------------------- */
/*      Collect the geometries from this layer, and build list of burn values.        */
/* ---------------------------------------------------------------------------------- */
    OGRFeatureH hFeat;
    std::vector<OGRGeometryH> ahGeometries;
    std::vector<double> adfFullBurnValues;

    OGR_L_ResetReading( hSrcLayer );

    while( (hFeat = OGR_L_GetNextFeature( hSrcLayer )) != NULL )
    {
        OGRGeometryH hGeom;

        if( OGR_F_GetGeometryRef( hFeat ) == NULL )
        {
            OGR_F_Destroy( hFeat );
            continue;
        }

        hGeom = OGR_G_Clone( OGR_F_GetGeometryRef( hFeat ) );
        ahGeometries.push_back( hGeom );

        for( unsigned int iBand = 0; iBand < anBandList.size(); iBand++ )
        {
            if( adfBurnValues.size() > 0 )
                adfFullBurnValues.push_back(adfBurnValues[MIN(iBand,adfBurnValues.size()-1)] );
            else if( pszBurnAttribute )
            {
                adfFullBurnValues.push_back( OGR_F_GetFieldAsDouble( hFeat, iBurnField ) );
            }
        }

        OGR_F_Destroy( hFeat );
    }


/* -------------------------------------------------------------------- */
/*      Perform the burn.                                               */
/* -------------------------------------------------------------------- */
    hpgc_RasterizeGeometries( hDstDS, anBandList.size(), &(anBandList[0]),
                             ahGeometries.size(), &(ahGeometries[0]),
                             &(adfFullBurnValues[0]),cp,np );

/* -------------------------------------------------------------------- */
/*      Cleanup geometries.                                             */
/* -------------------------------------------------------------------- */
    int iGeom;

    for( iGeom = ahGeometries.size()-1; iGeom >= 0; iGeom-- )
        OGR_G_DestroyGeometry( ahGeometries[iGeom] );


}

/************************************************************************/
/*                  CreateOutputDataset()                               */
///			通用创建栅格数据集函数，能够指定参考坐标、外界矩形
///			栅格分辨率、波段数、栅格值类型、初始值以及nodata值
/************************************************************************/

static GDALDatasetH CreateOutputDataset(GDALDriverH hDriver, const char* pszDstFilename, 
										OGRSpatialReferenceH hSRS,OGREnvelope sEnvelop,
										int nXSize, int nYSize,double dfXRes, double dfYRes, 
										int nBandCount,GDALDataType eOutputType,
										std::vector<double> adfInitVals,  double dfNoData) 
{
	int bFirstLayer = TRUE;
	int bTargetAlignedPixels=FALSE;
	char* pszWKT = NULL;
	GDALDatasetH hDstDS = NULL;
	double adfProjection[6];

	if (dfXRes == 0 && dfYRes == 0) {
		dfXRes = (sEnvelop.MaxX - sEnvelop.MinX) / nXSize;
		dfYRes = (sEnvelop.MaxY - sEnvelop.MinY) / nYSize;
	}

	adfProjection[0] = sEnvelop.MinX;
	adfProjection[1] = dfXRes;
	adfProjection[2] = 0;
	adfProjection[3] = sEnvelop.MaxY;
	adfProjection[4] = 0;
	adfProjection[5] = -dfYRes;

	if (nXSize == 0 && nYSize == 0) {
		nXSize = (int) (0.5 + (sEnvelop.MaxX - sEnvelop.MinX) / dfXRes);
		nYSize = (int) (0.5 + (sEnvelop.MaxY - sEnvelop.MinY) / dfYRes);
	}

	hDstDS = GDALCreate(hDriver, pszDstFilename, nXSize, nYSize, 
			nBandCount,eOutputType, NULL);

	if (hDstDS == NULL)
	{
		fprintf(stderr, "Cannot create %s\n", pszDstFilename);
		exitMPI(2);
	}

	GDALSetGeoTransform(hDstDS, adfProjection);

	if (hSRS)
		OSRExportToWkt(hSRS, &pszWKT);
	if (pszWKT)
		GDALSetProjection(hDstDS, pszWKT);
	CPLFree(pszWKT);
	
	int iBand;

	if (dfNoData!=0) {
		for (iBand = 0; iBand < nBandCount; iBand++) {
			GDALRasterBandH hBand = GDALGetRasterBand(hDstDS, iBand + 1);
			GDALSetRasterNoDataValue(hBand, dfNoData);
		}
	}

	if (adfInitVals.size() != 0) {
		for (iBand = 0; iBand < MIN(nBandCount,(int)adfInitVals.size());
				iBand++) {
			GDALRasterBandH hBand = GDALGetRasterBand(hDstDS, iBand + 1);
			GDALFillRaster(hBand, adfInitVals[iBand], 0);
		}
	}

	return hDstDS;
}

/************************************************************************/
/*                                main()                                */
/************************************************************************/

int main(int argc, char ** argv)

{
	const char *pszSrcFilename = NULL;
	const char *pszDstFilename = NULL;
	char **papszLayers = NULL;
	const char *pszBurnAttribute = NULL;
	std::vector<int> anBandList;
	std::vector<double> adfBurnValues;
	std::vector<double> adfInitVals;
	char **papszRasterizeOptions = NULL;
	int bCreateOutput = FALSE;
	const char* pszFormat = "GTiff";
	GDALDriverH hDriver = NULL;
	GDALDataType eOutputType = GDT_Byte;
	int bNoDataSet = FALSE;
	double dfNoData = 0;
	int nXSize = 0, nYSize = 0;
	double dfXRes = 0, dfYRes = 0;

	int i,cp, np;
	double time1,time2;

//MPI_Status status;
	//argv[0]="hpgc_rasterize";
	//argv[1]="-burn";
	//argv[2]="255";
	//argv[3]="-l";
	//argv[4]="many_polygons";
	//argv[5]="-ts";
	//argv[6]="1000";
	//argv[7]="1000";
	//argv[8]="-ot";
	//argv[9]="Float32";
	////argv[10]="MYSQL:shandong_DB,user=root,password=123456,host=127.0.0.1,port=3306" ;
	//argv[10]="d:\\data\\many_polygons.shp";
	//argv[11]="many_polygons.tif";
	//argc=12;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &cp);
	MPI_Comm_size(MPI_COMM_WORLD, &np);

	//check the time at start point
	time1=MPI_Wtime();

	/* Check that we are running against at least GDAL 1.4 */
	/* Note to developers : if we use newer API, please change the requirement */
	if (atoi(GDALVersionInfo("VERSION_NUM")) < 1800) {
		if(cp==0)
		fprintf(stderr,
				"At least, GDAL >= 1.8.0 is required for this version of %s, "
						"which was compiled against GDAL %s\n", argv[0],
				GDAL_RELEASE_NAME);
		exitMPI(1);
	}

	GDALAllRegister();
	OGRRegisterAll();

	argc = GDALGeneralCmdLineProcessor(argc, &argv, 0);
	if (argc < 1)
		exitMPI(-argc);

//* -------------------------------------------------------------------- */
//*      Parse arguments.                                                */
//* -------------------------------------------------------------------- */
	for (i = 1; i < argc; i++) {
		if (EQUAL(argv[i],"-b") && i < argc - 1) {
			if (strchr(argv[i + 1], ' ')) {
				char** papszTokens = CSLTokenizeString(argv[i + 1]);
				char** papszIter = papszTokens;
				while (papszIter && *papszIter) {
					anBandList.push_back(atoi(*papszIter));
					papszIter++;
				}
				CSLDestroy(papszTokens);
				i += 1;
			} else {
				while (i < argc - 1 && ArgIsNumeric(argv[i + 1])) {
					anBandList.push_back(atoi(argv[i + 1]));
					i += 1; 
				}
			}
		}else if( EQUAL(argv[i],"-a") && i < argc-1 )
        {
            pszBurnAttribute = argv[++i];
        }else if (EQUAL(argv[i],"-burn") && i < argc - 1) {
			if (strchr(argv[i + 1], ' ')) {
				char** papszTokens = CSLTokenizeString(argv[i + 1]);
				char** papszIter = papszTokens;
				while (papszIter && *papszIter) {
					adfBurnValues.push_back(atof(*papszIter));
					papszIter++;
				}
				CSLDestroy(papszTokens);
				i += 1;
			} else {
				while (i < argc - 1 && ArgIsNumeric(argv[i + 1])) {
					adfBurnValues.push_back(atof(argv[i + 1]));
					i += 1; 
				}
			}
		} else if (EQUAL(argv[i],"-l") && i < argc - 1) {
			papszLayers = CSLAddString(papszLayers, argv[++i]);
		} else if (EQUAL(argv[i],"-of") && i < argc - 1) {
			pszFormat = argv[++i];
			bCreateOutput = TRUE;
		} else if (EQUAL(argv[i],"-init") && i < argc - 1) {
			if (strchr(argv[i + 1], ' ')) {
				char** papszTokens = CSLTokenizeString(argv[i + 1]);
				char** papszIter = papszTokens;
				while (papszIter && *papszIter) {
					adfInitVals.push_back(atof(*papszIter));
					papszIter++;
				}
				CSLDestroy(papszTokens);
				i += 1;
			} else {
				while (i < argc - 1 && ArgIsNumeric(argv[i + 1])) {
					adfInitVals.push_back(atof(argv[i + 1]));
					i += 1;
				}
			}
			bCreateOutput = TRUE;
		} else if (EQUAL(argv[i],"-a_nodata") && i < argc - 1) {
			dfNoData = atof(argv[i + 1]);
			bNoDataSet = TRUE;
			i += 1;
			bCreateOutput = TRUE;
		} else if (EQUAL(argv[i],"-ot") && i < argc - 1) {
			int iType;

			for (iType = 1; iType < GDT_TypeCount; iType++) {
				if (GDALGetDataTypeName((GDALDataType) iType) != NULL
						&& EQUAL(GDALGetDataTypeName((GDALDataType)iType),
								argv[i+1])) {
					eOutputType = (GDALDataType) iType;
				}
			}

			if (eOutputType == GDT_Unknown) {
				if(cp==0)
				{printf("Unknown output pixel type: %s\n", argv[i + 1]);
				Usage();}
				exitMPI(1);
			}
			i++;
			bCreateOutput = TRUE;
		} else if ((EQUAL(argv[i],"-ts") || EQUAL(argv[i],"-outsize"))
				&& i < argc - 2) {
			nXSize = atoi(argv[++i]);
			nYSize = atoi(argv[++i]);
			if (nXSize <= 0 || nYSize <= 0) {
				if(cp==0){
				printf("Wrong value for -outsize parameters\n");
				Usage();}
				exitMPI(1);
			}
			bCreateOutput = TRUE;
		} else if (EQUAL(argv[i],"-tr") && i < argc - 2) {
			dfXRes = fabs(atof(argv[++i]));
			dfYRes = fabs(atof(argv[++i]));
			if (dfXRes == 0 || dfYRes == 0) {
				if(cp==0){
				printf("Wrong value for -tr parameters\n");
				Usage();}
				exitMPI(1);
			}
			bCreateOutput = TRUE;
		} else if (pszSrcFilename == NULL) {
			pszSrcFilename = argv[i];
		} else if (pszDstFilename == NULL) {
			pszDstFilename = argv[i];
		} else
			{	fprintf(stderr, "Bad parameters.\n\n");
				if(cp==0)
					Usage();
				exitMPI(1);
			}
	}

	if (pszSrcFilename == NULL || pszDstFilename == NULL) {
		if(cp==0){
		fprintf(stderr, "Missing source or destination.\n\n");
		Usage();}
		exitMPI(1);
	}
	if ( papszLayers == NULL) {
		if(cp==0){
		fprintf(stderr, "At least one of -l or -sql required.\n\n");
		Usage();}
		exitMPI(1);
	}

	if (adfBurnValues.size() == 0 && pszBurnAttribute == NULL ) {
		if(cp==0){
		fprintf(stderr, "At least one of -burn or -a required.\n\n");
		Usage();}
		exitMPI(1);
	}

	if (bCreateOutput) {
		if (dfXRes == 0 && dfYRes == 0 && nXSize == 0 && nYSize == 0) {
			if(cp==0){
			fprintf(stderr,
					"'-tr xres yes' or '-ts xsize ysize' is required.\n\n");
			Usage();}
			exitMPI(1);
		}

		if (anBandList.size() != 0) {
			if(cp==0){
			fprintf(
					stderr,
					"-b option cannot be used when creating a GDAL dataset.\n\n");
			Usage();}
			exitMPI(1);
		}

		//if the creation of raster-dataset is needed, how many bands it should be?
		//nBandCount:the larger one in adfBurnValues.size() and adfInitVals.size().
		int nBandCount = 1;
		if(adfInitVals.size()==0)
			adfInitVals.push_back(0);

		if (adfBurnValues.size() != 0)
			nBandCount = adfBurnValues.size();

		if ((int) adfInitVals.size() > nBandCount)
			nBandCount = adfInitVals.size();
		else if ((int) adfInitVals.size() < nBandCount)
		{
			for (i = adfInitVals.size(); i < nBandCount ; i++)
				adfInitVals.push_back(adfInitVals[0]);
		}

		for (i = 1; i <= nBandCount; i++)
			anBandList.push_back(i);

	} else {
		if (anBandList.size() == 0)
			anBandList.push_back(1);
	}

//* -------------------------------------------------------------------- */
//*      Open source vector dataset.                                     */
//* -------------------------------------------------------------------- */
	OGRDataSourceH hSrcDS;

	hSrcDS = OGROpen(pszSrcFilename, FALSE, NULL);
	if (hSrcDS == NULL) {
		if(cp==0)
		fprintf(stderr, "Failed to open feature source: %s\n", pszSrcFilename);
		
		exitMPI(1);
	}else{//检查矢量数据文件几何类型，扫描线处理面数据
		OGRLayerH hLayer = OGR_DS_GetLayerByName(hSrcDS, papszLayers[0]);
		if (hLayer == NULL) 
		{
			if(cp==0)
				fprintf(stderr, "Unable to find layer %s, exitting...\n",papszLayers[0]);
			exitMPI(1);
		}

		OGR_L_ResetReading( hLayer );
		OGRFeatureH hFeat;
		while( (hFeat = OGR_L_GetNextFeature( hLayer )) != NULL )
		{//获得图层几何类型
			OGRGeometry * pGeom=NULL;
			pGeom=(OGRGeometry *)OGR_F_GetGeometryRef( hFeat );

			if( pGeom == NULL )
			{
				OGR_F_Destroy( hFeat );
				continue;
			}else{
				switch ( wkbFlatten(pGeom->getGeometryType()) )
				{//检测图层几何类型
					case wkbLineString:
					case wkbMultiLineString:	
						//fprintf(stderr, "wkbLineString\n");
						break;
					default:
						{
							if(cp==0)
								fprintf(stderr, "The geometry type is surpposed to be line, exitting...\n");
							exitMPI(1);
						}
				}
				OGR_F_Destroy( hFeat );
				break;
			}			
		}
	}

//* ------------------------------------------------------------------------------- */
//*      Open target raster file.  Eventually we will add optional creation.        */
//* ------------------------------------------------------------------------------- */
	GDALDatasetH hDstDS = NULL;
	int nLayerCount = CSLCount(papszLayers);
	if(cp==0)
	{
		fstream _file;
		_file.open(pszDstFilename,ios::in);

		//hDstDS = GDALOpen(pszDstFilename, GA_Update);
		if (!_file)//the target raster file does not exist,so needing to create it.
		{
			//* -------------------------------------------------------------------- */
			//*      Find the output driver.                                         */
			//* -------------------------------------------------------------------- */
			hDriver = GDALGetDriverByName(pszFormat);
			
			if (hDriver == NULL|| GDALGetMetadataItem(hDriver, GDAL_DCAP_CREATE, NULL) == NULL) 
			{//if the pszFormat is not a proper one, give the driver list!
				int iDr;
				if(cp==0)
				{
					printf("Output driver '%s' not recognised or does not support\n",pszFormat);
					printf("direct output file creation.  The following format drivers are configured\n"
									"and support direct output:\n");

					for (iDr = 0; iDr < GDALGetDriverCount(); iDr++) 
					{
						GDALDriverH hDriver = GDALGetDriver(iDr);
						if (GDALGetMetadataItem(hDriver, GDAL_DCAP_CREATE, NULL)!= NULL) 
							printf("  %s: %s\n", GDALGetDriverShortName(hDriver),GDALGetDriverLongName(hDriver));
					}

					printf("\n");
				}
				exitMPI(1);
			}

			OGREnvelope sEnvelop;
			OGRSpatialReferenceH hSRS=NULL;
			int nBandcount=anBandList.size();
			int bFirstLayer=TRUE;


			for (i = 0; i < nLayerCount; i++) 
			{//this loop is trying to get the largest Envelope in the input layers.
				OGREnvelope sLayerEnvelop;
				OGRLayerH hLayer = OGR_DS_GetLayerByName(hSrcDS, papszLayers[i]);
				if (hLayer == NULL)	continue;

				if (OGR_L_GetExtent(hLayer, &sLayerEnvelop, FALSE) != OGRERR_NONE)
				{
					fprintf(stderr, "Cannot get layer extent\n");
					exitMPI(2);
				}			

				if (bFirstLayer) 
				{
					sEnvelop.MinX = sLayerEnvelop.MinX;
					sEnvelop.MinY = sLayerEnvelop.MinY;
					sEnvelop.MaxX = sLayerEnvelop.MaxX;
					sEnvelop.MaxY = sLayerEnvelop.MaxY;
					bFirstLayer = FALSE;
				} else {
					sEnvelop.MinX = MIN(sEnvelop.MinX, sLayerEnvelop.MinX);
					sEnvelop.MinY = MIN(sEnvelop.MinY, sLayerEnvelop.MinY);
					sEnvelop.MaxX = MAX(sEnvelop.MaxX, sLayerEnvelop.MaxX);
					sEnvelop.MaxY = MAX(sEnvelop.MaxY, sLayerEnvelop.MaxY);
				}
			}
			
			//添加对gdos-MYSQL空间数据库的支持
			string pstr(pszSrcFilename);
			if(EQUAL(pstr.substr(0,6).c_str(),"MYSQL:"))
			{//try to get the srs of a gdao-mysql-datasource
				char* oHost,*oPassword,*oUser,*oDB, **papszTableNames;
				int nPort,j;
				char **papszItems = CSLTokenizeString2( pszSrcFilename+6, ",", CSLT_HONOURSTRINGS );

				if( CSLCount(papszItems) < 1 )
				{
					CSLDestroy( papszItems );
					CPLError( CE_Failure, CPLE_AppDefined, "MYSQL: request missing databasename." );
					exitMPI(0);
				}

				oDB=papszItems[0];

				for( j = 1; papszItems[j] != NULL; j++ )
				{
					if( EQUALN(papszItems[j],"user=",5) )
						oUser = papszItems[j] + 5;
					else if( EQUALN(papszItems[j],"password=",9) )
						oPassword = papszItems[j] + 9;
					else if( EQUALN(papszItems[j],"host=",5) )
						oHost = papszItems[j] + 5;
					else if( EQUALN(papszItems[j],"port=",5) )
						nPort = atoi(papszItems[j] + 5);
					else if( EQUALN(papszItems[j],"tables=",7) )
					{
						papszTableNames = CSLTokenizeStringComplex( 
							papszItems[j] + 7, ";", FALSE, FALSE );
					}
					else
						CPLError( CE_Warning, CPLE_AppDefined, 
								  "'%s' in MYSQL datasource definition not recognised and ignored.", papszItems[i] );
				}

				gts::GTGDOSMySQLDataSource *pGTSrc= new GTGDOSMySQLDataSource();
				if(pGTSrc->openDataSource("Mysql", oUser, oPassword, oHost,nPort,oDB))
				{//gdos不稳定，在win环境下Mysql可用，在linux环境下0；
					GTFeatureLayer *pGTSrcLyr = NULL;
					pGTSrcLyr=pGTSrc->getFeatureLayerByName(papszLayers[0], false);
					if(pGTSrcLyr!=NULL)
					{
						GTSpatialReference *pGTSrcSRS=NULL;
						pGTSrcSRS=pGTSrcLyr->getLayerSpatialRefPtr();
						if(pGTSrcSRS!=NULL)
							hSRS=(OGRSpatialReferenceH)pGTSrcSRS->getOGRSpatialRefPtr();
					}
				}else if(pGTSrc->openDataSource(0, oUser, oPassword, oHost,nPort,oDB))
				{
					GTFeatureLayer *pGTSrcLyr = NULL;
					pGTSrcLyr = pGTSrc->getFeatureLayerByName(papszLayers[0], false);
					if(pGTSrcLyr!=NULL)
					{
						GTSpatialReference *pGTSrcSRS=NULL;
						pGTSrcSRS=pGTSrcLyr->getLayerSpatialRefPtr();
						if(pGTSrcSRS!=NULL)
							hSRS=(OGRSpatialReferenceH)pGTSrcSRS->getOGRSpatialRefPtr();
					}
				}else
				{
					printf("Warning: Could not get the spatial reference information"
							"from the database!\n");
				}

			}else{
				OGRLayerH hLayer = OGR_DS_GetLayerByName(hSrcDS, papszLayers[0]);
				hSRS = OGR_L_GetSpatialRef(hLayer);
			}

			hDstDS = CreateOutputDataset(hDriver, pszDstFilename, hSRS, sEnvelop,
										nXSize, nYSize, dfXRes, dfYRes,nBandcount,eOutputType,
										adfInitVals, dfNoData);

			hDriver = NULL;		
			OGR_DS_Destroy(hDstDS);
		}else
		{//如果已经存在关闭文件
			_file.close();
		}
	}
	//At this section the target raster file does exist.
	MPI_Barrier(MPI_COMM_WORLD);

	hDstDS = GDALOpen(pszDstFilename, GA_Update);
	if (hDstDS == NULL)
		exitMPI(2);

	//====================================================================
	//
	//通过空间查询筛选多边形
	//
	//====================================================================

    int nYChunkSize,iY;
	int pulx,puly,plrx,plry;//像素坐标
	double dulx,duly,dlrx,dlry;//地理坐标
	double gT[6];//仿射变换系数

	//check the time at end point


	GDALGetGeoTransform(hDstDS,gT);

	nYChunkSize =(int) ceil(((GDALDataset*)hDstDS)->GetRasterYSize()/(double)np);
	iY=cp*nYChunkSize;

    if( nYChunkSize + iY >( (GDALDataset*)hDstDS)->GetRasterYSize() )
        nYChunkSize = ((GDALDataset*)hDstDS)->GetRasterYSize() - iY;
	//计算栅格块的左上角点和右下角点——栅格行列坐标
	pulx=0;
	puly=iY;
	plrx=((GDALDataset*)hDstDS)->GetRasterXSize()-1;
	plry=iY+nYChunkSize-1;

	//计算栅格块的左上角点和右下角点——栅格地理坐标
	dulx=gT[0] + gT[1] * pulx + gT[2]*puly;
	duly=gT[3] + gT[4] * pulx + gT[5]*puly;

	dlrx=gT[0] + gT[1] * plrx + gT[2]*plry;
	dlry=gT[3] + gT[4] * plrx + gT[5]*plry;

	OGRGeometry *poSpatialFilter=NULL;
	OGRLinearRing  oRing;

	oRing.addPoint( dulx, duly);
	oRing.addPoint( dulx, dlry);
	oRing.addPoint( dlrx, dlry);
	oRing.addPoint( dlrx, duly);
	oRing.addPoint( dulx, duly);

	poSpatialFilter = OGRGeometryFactory::createGeometry(wkbPolygon);
	((OGRPolygon *) poSpatialFilter)->addRing( &oRing );
	

//* -------------------------------------------------------------------- */
//*      Process each layer.                                             */
//* -------------------------------------------------------------------- */

	for (i = 0; i < nLayerCount; i++) 
	{
		OGRLayerH hLayer = OGR_DS_GetLayerByName(hSrcDS, papszLayers[i]);
		if (hLayer == NULL) 
		{
			if(cp==0)
				fprintf(stderr, "Unable to find layer %s, skipping.\n",papszLayers[i]);
			continue;
		}
		if(cp==0)
		{
			printf("\nThe total feature size of this layer is %d.\n"
					"The X size of the destination raster is %d.\n"
					"The Y size of the destination raster is %d.\n",
						OGR_L_GetFeatureCount(hLayer,true),
						((GDALDataset*)hDstDS)->GetRasterXSize(),
						((GDALDataset*)hDstDS)->GetRasterYSize());
		}
		//通过构建栅格块边界多边形，对矢量数据进行空间筛选
		OGR_L_SetSpatialFilter (hLayer,(OGRGeometryH )poSpatialFilter);

		//printf("%d",OGR_L_GetFeatureCount(hLayer,1));
		ProcessLayer(hLayer, hDstDS, anBandList,adfBurnValues, pszBurnAttribute,cp,np);
		
		time2=MPI_Wtime();
		char processor_name[MPI_MAX_PROCESSOR_NAME];
		int namelen;
		MPI_Get_processor_name(processor_name,&namelen);

		printf("\nThis is processing < No.%d.> on [%s]:\n"
				"My nYChunk Size:   ( %d ).\n"
				"My upper left:     ( %d,%d ).\n"
				"My lower right:    ( %d,%d ).\n"
				"My feature count:  ( %d ).\n"
				"Job's been done in < %f > seconds!\n",
				cp,processor_name,nYChunkSize,pulx,puly,plrx,plry,OGR_L_GetFeatureCount(hLayer,true),time2-time1);
	}


	
	//log
	ofstream logs;
	char *filename="rasterize_test.log";
	int len=strlen(filename);
	char *tempstr=(char*) malloc(sizeof(char)*(len+10));
	strcpy(tempstr,filename);
	char post_fix[10];
	int p;
	for(p=len-1;p>=0;p--)
		if(filename[p]=='.')
			break;
	if(p==0)
		post_fix[0]='\0';
	else
	{
		strcpy(post_fix,filename+p);
		for(;p<len-1;p++)
			tempstr[p]='\0';
	}
	char npstr[6],cpstr[6];
	sprintf(npstr,"%d",np);
	sprintf(cpstr,"%d",cp);
	
	strcat(tempstr,"_");
	strcat(tempstr,npstr);
	strcat(tempstr,"_");
	strcat(tempstr,cpstr);
	strcat(tempstr,post_fix);

	//cout<<filename<<endl;
	logs.open(tempstr,ios::app);
	logs<<np<<"\t"<<cp<<"\t"<<time2-time1<<endl;
	logs.close();
//* -------------------------------------------------------------------- */
//*      Cleanup                                                         */
//* -------------------------------------------------------------------- */
	OGRGeometryFactory::destroyGeometry(poSpatialFilter);
	OGR_DS_Destroy(hSrcDS);
	GDALClose(hDstDS);

	CSLDestroy(argv);
	CSLDestroy(papszRasterizeOptions);
	CSLDestroy(papszLayers);

	GDALDestroyDriverManager();
	OGRCleanupAll();

	MPI_Finalize();
	return 0;
}