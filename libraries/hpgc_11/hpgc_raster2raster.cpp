
/******************************************************************************
 * $Id: gdal_translate.cpp 21386 2011-01-03 20:17:11Z rouault $
 *
 * Project:  GDAL Utilities
 * Purpose:  GDAL Image Translator Program
 * Author:   Frank Warmerdam, warmerdam@pobox.com
 *
 ******************************************************************************
 * Copyright (c) 1998, 2002, Frank Warmerdam
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
#include <iostream>
#include "cpl_vsi.h"
#include "cpl_conv.h"
#include "cpl_string.h"
#include "gdal_priv.h"
#include "ogr_spatialref.h"
#include "vrtdataset.h"
#include "mpi.h"

#define MASTER 0
#define IOMASTER 1

using namespace std;
CPL_CVSID("$Id: gdal_translate.cpp 21386 2011-01-03 20:17:11Z rouault $");

static int ArgIsNumeric( const char * );
static void AttachMetadata( GDALDatasetH, char ** );
static void CopyBandInfo( GDALRasterBand * poSrcBand, GDALRasterBand * poDstBand,
                            int bCanCopyStatsMetadata, int bCopyScale, int bCopyNoData );
static int bSubCall = FALSE;

/*  ******************************************************************* */
/*                               Usage()                                */
/* ******************************************************************** */

static void Usage()

{
    int	iDr;

    printf( "Usage: gdal_translate [--help-general]\n"
            "       [-ot {Byte/Int16/UInt16/UInt32/Int32/Float32/Float64/\n"
            "             CInt16/CInt32/CFloat32/CFloat64}] [-strict]\n"
            "       [-of format] [-b band] [-mask band] [-expand {gray|rgb|rgba}]\n"
            "       [-unscale] [-scale [src_min src_max [dst_min dst_max]]]\n"
            "       [-a_srs srs_def][-a_nodata value]\n"
            "       [-gcp pixel line easting northing [elevation]]*\n"
            "       [-mo \"META-TAG=VALUE\"]* [-q] [-sds]\n"
            "       [-co \"NAME=VALUE\"]* [-stats]\n"
            "       src_dataset dst_dataset\n\n" );

    printf( "%s\n\n", GDALVersionInfo( "--version" ) );
    printf( "The following format drivers are configured and support output:\n" );
    for( iDr = 0; iDr < GDALGetDriverCount(); iDr++ )
    {
        GDALDriverH hDriver = GDALGetDriver(iDr);

        if( GDALGetMetadataItem( hDriver, GDAL_DCAP_CREATE, NULL ) != NULL
            || GDALGetMetadataItem( hDriver, GDAL_DCAP_CREATECOPY,
                                    NULL ) != NULL )
        {
            printf( "  %s: %s\n",
                    GDALGetDriverShortName( hDriver ),
                    GDALGetDriverLongName( hDriver ) );
        }
    }
}
void exitMPI(int code)
{
	MPI_Finalize();
	exit(code);
}
/************************************************************************/
/*                             ProxyMain()                              */
/************************************************************************/

enum
{
    MASK_DISABLED,
    MASK_AUTO,
    MASK_USER
};

/************************************************************************/
/*                  CreateOutputDataset()                               */
/************************************************************************/

static
GDALDatasetH CreateOutputDataset(OGRSpatialReferenceH hSRS,
                                 GDALDriverH hDriver, const char* pszDstFilename,
                                 int nXSize, int nYSize,
                                 int nBandCount, GDALDataType eOutputType,
                                 char** papszCreateOptions,
                                 int bNoDataSet, double dfNoData)
{

    char* pszWKT = NULL;
    GDALDatasetH hDstDS = NULL;


    hDstDS = GDALCreate(hDriver, pszDstFilename, nXSize, nYSize,
                        nBandCount, eOutputType, papszCreateOptions);
    if (hDstDS == NULL)
    {
        fprintf(stderr, "Cannot create %s\n", pszDstFilename);
        exit(2);
    }

    if (hSRS)
        OSRExportToWkt(hSRS, &pszWKT);
    if (pszWKT)
        GDALSetProjection(hDstDS, pszWKT);
    CPLFree(pszWKT);

    int iBand;
    /*if( nBandCount == 3 || nBandCount == 4 )
    {
        for(iBand = 0; iBand < nBandCount; iBand++)
        {
            GDALRasterBandH hBand = GDALGetRasterBand(hDstDS, iBand + 1);
            GDALSetRasterColorInterpretation(hBand, (GDALColorInterp)(GCI_RedBand + iBand));
        }
    }*/

    if (bNoDataSet)
    {
        for(iBand = 0; iBand < nBandCount; iBand++)
        {
            GDALRasterBandH hBand = GDALGetRasterBand(hDstDS, iBand + 1);
            GDALSetRasterNoDataValue(hBand, dfNoData);
            GDALFillRaster(hBand, 0, 0);
        }
    }



    return hDstDS;
}

/************************************************************************/
/*                     GDALDatasetCopyWholeRaster()                     */
/************************************************************************/

/**
 * \brief Copy all dataset raster data.
 *
 * This function copies the complete raster contents of one dataset to
 * another similarly configured dataset.  The source and destination
 * dataset must have the same number of bands, and the same width
 * and height.  The bands do not have to have the same data type.
 *
 * This function is primarily intended to support implementation of
 * driver specific CreateCopy() functions.  It implements efficient copying,
 * in particular "chunking" the copy in substantial blocks and, if appropriate,
 * performing the transfer in a pixel interleaved fashion.
 *
 * Currently the only papszOptions value supported are : "INTERLEAVE=PIXEL"
 * to force pixel interleaved operation and "COMPRESSED=YES" to force alignment
 * on target dataset block sizes to achieve best compression.  More options may be supported in
 * the future.
 *
 * @param hSrcDS the source dataset
 * @param hDstDS the destination dataset
 * @param papszOptions transfer hints in "StringList" Name=Value format.
 * @param pfnProgress progress reporting function.
 * @param pProgressData callback data for progress function.
 *
 * @return CE_None on success, or CE_Failure on failure.
 */

CPLErr CPL_STDCALL CopyWholeRaster(
    GDALDatasetH hSrcDS, GDALDatasetH hDstDS, int nulx,int nuly )
{

    GDALDataset *poSrcDS = (GDALDataset *) hSrcDS;
    GDALDataset *poDstDS = (GDALDataset *) hDstDS;
    CPLErr eErr = CE_None;

/* -------------------------------------------------------------------- */
/*      Confirm the datasets match in size and band counts.             */
/* -------------------------------------------------------------------- */
    int nXSize = poSrcDS->GetRasterXSize(),
        nYSize = poSrcDS->GetRasterYSize(),
        nBandCount = poSrcDS->GetRasterCount();

    if( poDstDS->GetRasterXSize() != nXSize
        || poDstDS->GetRasterCount() != nBandCount )
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "Input and output dataset sizes or band counts do not match " );
        return CE_Failure;
    }

/* -------------------------------------------------------------------- */
/*      Get our prototype band, and assume the others are similarly     */
/*      configured.                                                     */
/* -------------------------------------------------------------------- */
    if( nBandCount == 0 )
        return CE_None;

    GDALRasterBand *poDstPrototypeBand = poDstDS->GetRasterBand(1);
    GDALDataType eDT = poDstPrototypeBand->GetRasterDataType();

    int nPixelSize = (GDALGetDataTypeSize(eDT) / 8);
        nPixelSize *= nBandCount;

    void *pSwathBuf = VSIMalloc3(nXSize, nYSize, nPixelSize );

    if( pSwathBuf == NULL )
    {
        CPLError( CE_Failure, CPLE_OutOfMemory,
                "Failed to allocate %d*%d*%d byte swath buffer in\n"
                "GDALDatasetCopyWholeRaster()",
                nXSize, nYSize, nPixelSize );
        return CE_Failure;
    }

/* ==================================================================== */
/*      Band oriented (uninterleaved) case.                             */
/* ==================================================================== */

	for( int iBand = 0; iBand < nBandCount && eErr == CE_None; iBand++ )
	{
		int nBand = iBand+1;

		eErr = poSrcDS->RasterIO( GF_Read,
								0, 0, nXSize, nYSize,
								pSwathBuf, nXSize, nYSize,
								eDT, 1, &nBand,
								0, 0, 0 );

		if( eErr == CE_None )
			eErr = poDstDS->RasterIO( GF_Write,
									nulx, nuly, nXSize, nYSize,
									pSwathBuf, nXSize, nYSize,
									eDT, 1, &nBand,
									0, 0, 0 );
	 }

/* -------------------------------------------------------------------- */
/*      Cleanup                                                         */
/* -------------------------------------------------------------------- */
    CPLFree( pSwathBuf );

    return eErr;
}
typedef struct rasterblock
{
	long ID;
	int offx,offy,xsize,ysize;
	bool bdone;
};

typedef struct procinfo
{
	long PID,RID,BID;
	double start,end;
	bool bsuccess;
};

static int ProxyMain( int argc, char ** argv )

{
    GDALDatasetH		hDataset, hOutDS=NULL,hDistDataset;
    int					i;
    int					nRasterXSize, nRasterYSize;
    const char			*pszSource=NULL, *pszDest=NULL, *pszFormat = "GTiff";
    GDALDriverH			hDriver;
    int					*panBandList = NULL; /* negative value of panBandList[i] means mask band of ABS(panBandList[i]) */
    int         		nBandCount = 0, bDefBands = TRUE;
    GDALDataType		eOutputType = GDT_Unknown;
    int					nOXSize = 0, nOYSize = 0;

    char                **papszCreateOptions = NULL;
    int                 bStrict = FALSE;
    const char          *pszProjection;
    int                 bScale = FALSE, bHaveScaleSrc = FALSE, bUnscale=FALSE;
    double				dfScaleSrcMin=0.0, dfScaleSrcMax=255.0;
    double              dfScaleDstMin=0.0, dfScaleDstMax=255.0;
    char                **papszMetadataOptions = NULL;
    char                *pszOutputSRS = NULL;
    int                 bQuiet = false;
    GDALProgressFunc    pfnProgress = GDALTermProgress;
    int                 nGCPCount = 0;
    GDAL_GCP            *pasGCPs = NULL;
    int                 iSrcFileArg = -1, iDstFileArg = -1;
    int                 bCopySubDatasets = FALSE;
    int                 bSetNoData = FALSE;
    int                 bUnsetNoData = FALSE;
    double				dfNoDataReal = 0.0;
    int                 nRGBExpand = 0;
    int                 bParsedMaskArgument = FALSE;
    int                 eMaskMode = MASK_AUTO;
    int                 nMaskBand = 0; /* negative value means mask band of ABS(nMaskBand) */
    int                 bStats = FALSE, bApproxStats = FALSE;
	rasterblock pBLOCKINFO;
	procinfo pPROCINFO;

    /* Check strict compilation and runtime library version as we use C++ API */
    if (! GDAL_CHECK_VERSION(argv[0]))
        exit(1);

    /* Must process GDAL_SKIP before GDALAllRegister(), but we can't call */
    /* GDALGeneralCmdLineProcessor before it needs the drivers to be registered */
    /* for the --format or --formats options */
    for( i = 1; i < argc; i++ )
    {
        if( EQUAL(argv[i],"--config") && i + 2 < argc && EQUAL(argv[i + 1], "GDAL_SKIP") )
        {
            CPLSetConfigOption( argv[i+1], argv[i+2] );

            i += 2;
        }
    }

	double	time1,time2;
	int cp, np;
	MPI_Status status;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &cp);
	MPI_Comm_size(MPI_COMM_WORLD, &np);
	if(np<2)
	{
		printf("This Algorithm cannot be done with less then two processes! \nQuit!\n");
		MPI_Finalize();
        return 0;
	}
	time1=MPI_Wtime();

/* -------------------------------------------------------------------- */
/*      Register standard GDAL drivers, and process generic GDAL        */
/*      command options.                                                */
/* -------------------------------------------------------------------- */
    GDALAllRegister();
    argc = GDALGeneralCmdLineProcessor( argc, &argv, 0 );
    if( argc < 1 )
        exit( -argc );

    int bParaisOK=1, bcreatedDst=1;

    if(cp!=0)
	{
    	MPI_Recv(&bParaisOK,1,MPI_INT,0,1,MPI_COMM_WORLD,&status);

		if(bParaisOK==0)
    		{
    			MPI_Finalize();
    			exit(0);
    		}
	}
/* -------------------------------------------------------------------- */
/*      Handle command line arguments.                                  */
/* -------------------------------------------------------------------- */
    for( i = 1; i < argc; i++ )
    {
        if( EQUAL(argv[i], "--utility_version") )
        {
            printf("%s was compiled against GDAL %s and is running against GDAL %s\n",
                   argv[0], GDAL_RELEASE_NAME, GDALVersionInfo("RELEASE_NAME"));

            //parameters analyzing fail, will exit program
            bParaisOK=0;
            for(int ccp=1;ccp<np;ccp++)
				MPI_Send(&bParaisOK,1,MPI_INT,ccp,1,MPI_COMM_WORLD);

            MPI_Finalize();
            return 0;
        }
        else if( EQUAL(argv[i],"-of") && i < argc-1 )
            pszFormat = argv[++i];

        else if( EQUAL(argv[i],"-q") || EQUAL(argv[i],"-quiet") )
        {
            bQuiet = TRUE;
            pfnProgress = GDALDummyProgress;
        }

        else if( EQUAL(argv[i],"-ot") && i < argc-1 )
        {
            int	iType;

            for( iType = 1; iType < GDT_TypeCount; iType++ )
            {
                if( GDALGetDataTypeName((GDALDataType)iType) != NULL
                    && EQUAL(GDALGetDataTypeName((GDALDataType)iType),
                             argv[i+1]) )
                {
                    eOutputType = (GDALDataType) iType;
                }
            }

            if( eOutputType == GDT_Unknown )
            {
                printf( "Unknown output pixel type: %s\n", argv[i+1] );
                Usage();
                GDALDestroyDriverManager();

                //parameters analyzing fail, will exit program
                bParaisOK=0;
                for(int ccp=1;ccp<np;ccp++)
                	MPI_Send(&bParaisOK,1,MPI_INT,ccp,1,MPI_COMM_WORLD);

                MPI_Finalize();
                exit(2);
            }
            i++;
        }
        else if( EQUAL(argv[i],"-b") && i < argc-1 )
        {
            const char* pszBand = argv[i+1];
            int bMask = FALSE;
            if (EQUAL(pszBand, "mask"))
                pszBand = "mask,1";

            if (EQUALN(pszBand, "mask,", 5))
            {
                bMask = TRUE;
                pszBand += 5;
                /* If we use tha source mask band as a regular band */
                /* don't create a target mask band by default */
                if( !bParsedMaskArgument )
                    eMaskMode = MASK_DISABLED;
            }

            int nBand = atoi(pszBand);
            if( nBand < 1 )
            {
                printf( "Unrecognizable band number (%s).\n", argv[i+1] );
                Usage();
                GDALDestroyDriverManager();
                //parameters analyzing fail, will exit program
                bParaisOK=0;
                for(int ccp=1;ccp<np;ccp++)
                	MPI_Send(&bParaisOK,1,MPI_INT,ccp,1,MPI_COMM_WORLD);

                MPI_Finalize();
                exit( 2 );
            }
            i++;

            nBandCount++;
            panBandList = (int *)
                CPLRealloc(panBandList, sizeof(int) * nBandCount);

            panBandList[nBandCount-1] = nBand;
            if (bMask)
                panBandList[nBandCount-1] *= -1;

            if( panBandList[nBandCount-1] != nBandCount )
                bDefBands = FALSE;
        }
        else if( EQUAL(argv[i],"-mask") && i < argc-1 )
        {
            bParsedMaskArgument = TRUE;
            const char* pszBand = argv[i+1];
            if (EQUAL(pszBand, "none"))
            {
                eMaskMode = MASK_DISABLED;
            }
            else if (EQUAL(pszBand, "auto"))
            {
                eMaskMode = MASK_AUTO;
            }
            else
            {
                int bMask = FALSE;
                if (EQUAL(pszBand, "mask"))
                    pszBand = "mask,1";
                if (EQUALN(pszBand, "mask,", 5))
                {
                    bMask = TRUE;
                    pszBand += 5;
                }
                int nBand = atoi(pszBand);
                if( nBand < 1 )
                {
                    printf( "Unrecognizable band number (%s).\n", argv[i+1] );
                    Usage();
                    GDALDestroyDriverManager();
                    //parameters analyzing fail, will exit program
                    bParaisOK=0;
                    for(int ccp=1;ccp<np;ccp++)
                    	MPI_Send(&bParaisOK,1,MPI_INT,ccp,1,MPI_COMM_WORLD);

                    MPI_Finalize();
                    exit( 2 );
                }

                eMaskMode = MASK_USER;
                nMaskBand = nBand;
                if (bMask)
                    nMaskBand *= -1;
            }
            i ++;
        }
        else if( EQUAL(argv[i],"-not_strict")  )
            bStrict = FALSE;

        else if( EQUAL(argv[i],"-strict")  )
            bStrict = TRUE;

        else if( EQUAL(argv[i],"-sds")  )
            bCopySubDatasets = TRUE;

        else if( EQUAL(argv[i],"-gcp") && i < argc - 4 )
        {
            char* endptr = NULL;
            /* -gcp pixel line easting northing [elev] */

            nGCPCount++;
            pasGCPs = (GDAL_GCP *)
                CPLRealloc( pasGCPs, sizeof(GDAL_GCP) * nGCPCount );
            GDALInitGCPs( 1, pasGCPs + nGCPCount - 1 );

            pasGCPs[nGCPCount-1].dfGCPPixel = CPLAtofM(argv[++i]);
            pasGCPs[nGCPCount-1].dfGCPLine = CPLAtofM(argv[++i]);
            pasGCPs[nGCPCount-1].dfGCPX = CPLAtofM(argv[++i]);
            pasGCPs[nGCPCount-1].dfGCPY = CPLAtofM(argv[++i]);
            if( argv[i+1] != NULL
                && (CPLStrtod(argv[i+1], &endptr) != 0.0 || argv[i+1][0] == '0') )
            {
                /* Check that last argument is really a number and not a filename */
                /* looking like a number (see ticket #863) */
                if (endptr && *endptr == 0)
                    pasGCPs[nGCPCount-1].dfGCPZ = CPLAtofM(argv[++i]);
            }

            /* should set id and info? */
        }

        else if( EQUAL(argv[i],"-a_nodata") && i < argc - 1 )
        {
            if (EQUAL(argv[i+1], "none"))
            {
                bUnsetNoData = TRUE;
            }
            else
            {
                bSetNoData = TRUE;
                dfNoDataReal = CPLAtofM(argv[i+1]);
            }
            i += 1;
        }
        else if( EQUAL(argv[i],"-co") && i < argc-1 )
        {
            papszCreateOptions = CSLAddString( papszCreateOptions, argv[++i] );
        }

        else if( EQUAL(argv[i],"-scale") )
        {
            bScale = TRUE;
            if( i < argc-2 && ArgIsNumeric(argv[i+1]) )
            {
                bHaveScaleSrc = TRUE;
                dfScaleSrcMin = CPLAtofM(argv[i+1]);
                dfScaleSrcMax = CPLAtofM(argv[i+2]);
                i += 2;
            }
            if( i < argc-2 && bHaveScaleSrc && ArgIsNumeric(argv[i+1]) )
            {
                dfScaleDstMin = CPLAtofM(argv[i+1]);
                dfScaleDstMax = CPLAtofM(argv[i+2]);
                i += 2;
            }
            else
            {
                dfScaleDstMin = 0.0;
                dfScaleDstMax = 255.999;
            }
        }

        else if( EQUAL(argv[i], "-unscale") )
        {
            bUnscale = TRUE;
        }

        else if( EQUAL(argv[i],"-mo") && i < argc-1 )
        {
            papszMetadataOptions = CSLAddString( papszMetadataOptions,
                                                 argv[++i] );
        }
        else if( EQUAL(argv[i],"-a_srs") && i < argc-1 )
        {
            OGRSpatialReference oOutputSRS;

            if( oOutputSRS.SetFromUserInput( argv[i+1] ) != OGRERR_NONE )
            {
                fprintf( stderr, "Failed to process SRS definition: %s\n",
                         argv[i+1] );
                GDALDestroyDriverManager();
                //parameters analyzing fail, will exit program
                bParaisOK=0;
                for(int ccp=1;ccp<np;ccp++)
                	MPI_Send(&bParaisOK,1,MPI_INT,ccp,1,MPI_COMM_WORLD);

                MPI_Finalize();
                exit( 1 );
            }

            oOutputSRS.exportToWkt( &pszOutputSRS );
            i++;
        }

        else if( EQUAL(argv[i],"-expand") && i < argc-1 )
        {
            if (EQUAL(argv[i+1], "gray"))
                nRGBExpand = 1;
            else if (EQUAL(argv[i+1], "rgb"))
                nRGBExpand = 3;
            else if (EQUAL(argv[i+1], "rgba"))
                nRGBExpand = 4;
            else
            {
                printf( "Value %s unsupported. Only gray, rgb or rgba are supported.\n\n",
                    argv[i] );
                Usage();
                GDALDestroyDriverManager();
                //parameters analyzing fail, will exit program
                bParaisOK=0;
                for(int ccp=1;ccp<np;ccp++)
                	MPI_Send(&bParaisOK,1,MPI_INT,ccp,1,MPI_COMM_WORLD);

                MPI_Finalize();
                exit( 2 );
            }
            i++;
        }

        else if( EQUAL(argv[i], "-stats") )
        {
            bStats = TRUE;
            bApproxStats = FALSE;
        }
        else if( EQUAL(argv[i], "-approx_stats") )
        {
            bStats = TRUE;
            bApproxStats = TRUE;
        }

        else if( argv[i][0] == '-' )
        {
            printf( "Option %s incomplete, or not recognised.\n\n",
                    argv[i] );
            Usage();
            GDALDestroyDriverManager();

            //parameters analyzing fail, will exit program
            bParaisOK=0;
            for(int ccp=1;ccp<np;ccp++)
            	MPI_Send(&bParaisOK,1,MPI_INT,ccp,1,MPI_COMM_WORLD);

            MPI_Finalize();
            exit( 2 );
        }
        else if( pszSource == NULL )
        {
            iSrcFileArg = i;
            pszSource = argv[i];
        }
        else if( pszDest == NULL )
        {
            pszDest = argv[i];
            iDstFileArg = i;
        }

        else
        {
            printf( "Too many command options.\n\n" );
            Usage();
            GDALDestroyDriverManager();
            //parameters analyzing fail, will exit program
            bParaisOK=0;
            for(int ccp=1;ccp<np;ccp++)
            	MPI_Send(&bParaisOK,1,MPI_INT,ccp,1,MPI_COMM_WORLD);

            MPI_Finalize();
            exit( 2 );
        }
    }

    if( pszDest == NULL )
    {
        Usage();
        GDALDestroyDriverManager();
        //parameters analyzing fail, will exit program
        bParaisOK=0;
        for(int ccp=1;ccp<np;ccp++)
        	MPI_Send(&bParaisOK,1,MPI_INT,ccp,1,MPI_COMM_WORLD);

        MPI_Finalize();
        exit( 10 );
    }

    if ( strcmp(pszSource, pszDest) == 0)
    {
        fprintf(stderr, "Source and destination datasets must be different.\n");
        GDALDestroyDriverManager();
        //parameters analyzing fail, will exit program
        bParaisOK=0;
        for(int ccp=1;ccp<np;ccp++)
        	MPI_Send(&bParaisOK,1,MPI_INT,ccp,1,MPI_COMM_WORLD);

        MPI_Finalize();
        exit( 1 );
    }

/* -------------------------------------------------------------------- */
/*      Attempt to open source file.                                    */
/* -------------------------------------------------------------------- */

    hDataset = GDALOpenShared( pszSource, GA_ReadOnly );

    if( hDataset == NULL )
    {
        fprintf( stderr,
                 "GDALOpen failed - %d\n%s\n",
                 CPLGetLastErrorNo(), CPLGetLastErrorMsg() );
        GDALDestroyDriverManager();
        //parameters analyzing fail, will exit program
        bParaisOK=0;
        for(int ccp=1;ccp<np;ccp++)
        	MPI_Send(&bParaisOK,1,MPI_INT,ccp,1,MPI_COMM_WORLD);

        MPI_Finalize();
        exit( 1 );
    }
	
/* -------------------------------------------------------------------- */
/*      Handle subdatasets.                                             */
/* -------------------------------------------------------------------- */
    if( !bCopySubDatasets
        && CSLCount(GDALGetMetadata( hDataset, "SUBDATASETS" )) > 0
        && GDALGetRasterCount(hDataset) == 0 )
    {
        fprintf( stderr,
                 "Input file contains subdatasets. Please, select one of them for reading.\n" );
        GDALClose( hDataset );
        GDALDestroyDriverManager();
        //parameters analyzing fail, will exit program
        bParaisOK=0;
        for(int ccp=1;ccp<np;ccp++)
        	MPI_Send(&bParaisOK,1,MPI_INT,ccp,1,MPI_COMM_WORLD);

        MPI_Finalize();
        exit( 1 );
    }

    if( CSLCount(GDALGetMetadata( hDataset, "SUBDATASETS" )) > 0
        && bCopySubDatasets )
    {
        char **papszSubdatasets = GDALGetMetadata(hDataset,"SUBDATASETS");
        char *pszSubDest = (char *) CPLMalloc(strlen(pszDest)+32);
        int i;
        int bOldSubCall = bSubCall;
        char** papszDupArgv = CSLDuplicate(argv);
        int nRet = 0;

        CPLFree(papszDupArgv[iDstFileArg]);
        papszDupArgv[iDstFileArg] = pszSubDest;
        bSubCall = TRUE;
        for( i = 0; papszSubdatasets[i] != NULL; i += 2 )
        {
            CPLFree(papszDupArgv[iSrcFileArg]);
            papszDupArgv[iSrcFileArg] = CPLStrdup(strstr(papszSubdatasets[i],"=")+1);
            sprintf( pszSubDest, "%s%d", pszDest, i/2 + 1 );
            nRet = ProxyMain( argc, papszDupArgv );
            if (nRet != 0)
                break;
        }
        CSLDestroy(papszDupArgv);

        bSubCall = bOldSubCall;
        CSLDestroy(argv);

        GDALClose( hDataset );

        if( !bSubCall )
        {
            GDALDumpOpenDatasets( stderr );
            GDALDestroyDriverManager();
        }
        return nRet;
    }

/* -------------------------------------------------------------------- */
/*      Collect some information from the source file.                  */
/* -------------------------------------------------------------------- */
    nRasterXSize = GDALGetRasterXSize( hDataset );
    nRasterYSize = GDALGetRasterYSize( hDataset );

    if( cp==0 )
        printf( "Input file size is %d, %d\n", nRasterXSize, nRasterYSize );

/* -------------------------------------------------------------------- */
/*	Build band list to translate		                    			*/
/* -------------------------------------------------------------------- */
    if( nBandCount == 0 )
		{
			nBandCount = GDALGetRasterCount( hDataset );
			if( nBandCount == 0 )
			{
				fprintf( stderr, "Input file has no bands, and so cannot be translated.\n" );
				GDALDestroyDriverManager();
		        //parameters analyzing fail, will exit program
		        bParaisOK=0;
		        for(int ccp=1;ccp<np;ccp++)
		        	MPI_Send(&bParaisOK,1,MPI_INT,ccp,1,MPI_COMM_WORLD);
				exit(1 );
			}

			panBandList = (int *) CPLMalloc(sizeof(int)*nBandCount);
			for( i = 0; i < nBandCount; i++ )
				panBandList[i] = i+1;
		}
    else
		{
			for( i = 0; i < nBandCount; i++ )
			{
				if( ABS(panBandList[i]) > GDALGetRasterCount(hDataset) )
				{
					fprintf( stderr,
							 "Band %d requested, but only bands 1 to %d available.\n",
							 ABS(panBandList[i]), GDALGetRasterCount(hDataset) );
					GDALDestroyDriverManager();
			        //parameters analyzing fail, will exit program
			        bParaisOK=0;
			        for(int ccp=1;ccp<np;ccp++)
			        	MPI_Send(&bParaisOK,1,MPI_INT,ccp,1,MPI_COMM_WORLD);
					exit( 2 );
				}
			}

			if( nBandCount != GDALGetRasterCount( hDataset ) )
				bDefBands = FALSE;
		}

/* -------------------------------------------------------------------- */
/*      Find the output driver.                                         */
/* -------------------------------------------------------------------- */
    hDriver = GDALGetDriverByName( pszFormat );
    if( hDriver == NULL )
    {
        int	iDr;

        printf( "Output driver '%s' not recognised.\n", pszFormat );
        printf( "The following format drivers are configured and support output:\n" );
        for( iDr = 0; iDr < GDALGetDriverCount(); iDr++ )
        {
            GDALDriverH hDriver = GDALGetDriver(iDr);

            if( GDALGetMetadataItem( hDriver, GDAL_DCAP_CREATE, NULL ) != NULL
                || GDALGetMetadataItem( hDriver, GDAL_DCAP_CREATECOPY,
                                        NULL ) != NULL )
            {
                printf( "  %s: %s\n",
                        GDALGetDriverShortName( hDriver  ),
                        GDALGetDriverLongName( hDriver ) );
            }
        }
        printf( "\n" );
        Usage();

        GDALClose( hDataset );
        CPLFree( panBandList );
        GDALDestroyDriverManager();
        CSLDestroy( argv );
        CSLDestroy( papszCreateOptions );
        //parameters analyzing fail, will exit program
        bParaisOK=0;
        for(int ccp=1;ccp<np;ccp++)
        	MPI_Send(&bParaisOK,1,MPI_INT,ccp,1,MPI_COMM_WORLD);
        exit( 1 );
    }

    //parameters analyzing success, other process will start its job.
	if(cp==0)
	{
		bParaisOK=1;
		for(int ccp=1;ccp<np;ccp++)
    		MPI_Send(&bParaisOK,1,MPI_INT,ccp,1,MPI_COMM_WORLD);

		OGRSpatialReferenceH hSRS=OSRNewSpatialReference(NULL);

		if( pszOutputSRS != NULL )
		{
			OSRSetFromUserInput(hSRS, pszOutputSRS);
		}
		else
		{
			pszProjection = GDALGetProjectionRef( hDataset );
			if( pszProjection != NULL && strlen(pszProjection) > 0 )
				OSRSetFromUserInput(hSRS, pszProjection);
		}
		int nBand=nBandCount;

		if (nRGBExpand != 0)
		{
			if (nBand == 1)
				nBand = nRGBExpand;
			else if (nBand == 2 && (nRGBExpand == 3 || nRGBExpand == 4))
				nBand = nRGBExpand;
			else
			{
				fprintf(stderr, "Error : invalid use of -expand option.\n");
				MPI_Finalize();
				exit( 1 );
			}
		}
		/* -------------------------------------------------------------------- */
		/*      Select output data type to match source.                        */
		/* -------------------------------------------------------------------- */
		GDALDataType		ePixelType = eOutputType;
		if( ePixelType == GDT_Unknown )
		{
			GDALRasterBand  *poSrcBand;
			poSrcBand = ((GDALDataset *) hDataset)->GetRasterBand(1);
			ePixelType = poSrcBand->GetRasterDataType();
		}

		hDistDataset=CreateOutputDataset( hSRS,hDriver, pszDest,
										 nRasterXSize,  nRasterYSize,
										 nBand,  ePixelType,
										 papszCreateOptions,
										 bSetNoData,  dfNoDataReal);

		if( hDistDataset != NULL )
		{
			int bHasGotErr = FALSE;
			CPLErrorReset();
			GDALFlushCache( hDistDataset );
			if (CPLGetLastErrorType() != CE_None)
				bHasGotErr = TRUE;

			GDALClose( hDistDataset );
			if (bHasGotErr)
				hDistDataset = NULL;
		}
   
	}
//*****************************************************************************************
//构造自定义数据类型，定义MPI_PROCINFO,MPI_BLOCKINFO
//****************************************************************************************/
		MPI_Datatype MPI_PROCINFO,MPI_BLOCKINFO;
		int block_lens[4],proc_lens[4];
		MPI_Aint block_indices[4],proc_indices[4];
		MPI_Datatype block_old_types[4],proc_old_types[4];
		
		//the first block has three INT data, and others are only one.
		proc_lens[0] = 3;
		proc_lens[1] = 2;
		proc_lens[2] = 1;
		proc_lens[3] = 1;
		
		//Get the address of a location in memory
		MPI_Get_address( &pPROCINFO.PID , &proc_indices[0] );
		MPI_Get_address( &pPROCINFO.start , &proc_indices[1] );
		MPI_Get_address( &pPROCINFO.bsuccess , &proc_indices[2] );
		
		proc_indices[2] = proc_indices[2] - proc_indices[0];
		proc_indices[1] = proc_indices[1] - proc_indices[0];
		proc_indices[0] = 0;
		proc_indices[3]=sizeof(pPROCINFO);

		proc_old_types[0] = MPI_LONG;
		proc_old_types[1] = MPI_DOUBLE;
		proc_old_types[2] = MPI_CHAR;
		proc_old_types[3] = MPI_UB;

		MPI_Type_struct( 4, proc_lens, proc_indices, proc_old_types, &MPI_PROCINFO);
		MPI_Type_commit( &MPI_PROCINFO);

		//the first block has three INT data, and others are only one.
		block_lens[0] = 1;
		block_lens[1] = 4;
		block_lens[2] = 1;
		block_lens[3] = 1;
		
		//Get the address of a location in memory
		MPI_Get_address( &pBLOCKINFO.ID , &block_indices[0] );
		MPI_Get_address( &pBLOCKINFO.offx, &block_indices[1] );
		MPI_Get_address( &pBLOCKINFO.bdone, &block_indices[2] );
		
		block_indices[2] = block_indices[2] - block_indices[0];
		block_indices[1] = block_indices[1] - block_indices[0];
		block_indices[0] = 0;
		block_indices[3]=sizeof(pBLOCKINFO);

		block_old_types[0] = MPI_LONG;
		block_old_types[1] = MPI_INT;
		block_old_types[2] = MPI_CHAR;
		block_old_types[3] = MPI_UB;

		MPI_Type_struct( 4, block_lens, block_indices, block_old_types, &MPI_BLOCKINFO );
		MPI_Type_commit( &MPI_BLOCKINFO );
//*****************************************************************************************
//构造自定义数据类型，定义MPI_PROCINFO,MPI_BLOCKINFO
//****************************************************************************************/

	if(cp==0)
	{
		vector<rasterblock> Blocks;
		vector<procinfo> procinfos;
		vector<int> IdleProcs;
		vector<int> unDealBlocks;
		
//==================================================================================
//==================栅格数据划分策略============================================
//==================================================================================
		int nYChunkSize,iY,nScanlineBytes;
	
		GDALRasterBand *poBand = ((GDALDataset *) hDataset)->GetRasterBand(1);
		GDALDataType   eType;

		if( poBand->GetRasterDataType() == GDT_Byte )
			eType = GDT_Byte;
		else
			eType = GDT_Float32;
		
		//计算每行栅格数据的数据量，按行划分栅格数据
		nScanlineBytes = nBandCount * nRasterXSize* (GDALGetDataTypeSize(eType)/8);
		//10MB data,so how many line it should be?
		nYChunkSize = 10000000 / nScanlineBytes;

		if( nYChunkSize > nRasterYSize)
			nYChunkSize = nRasterYSize;
		else if(nYChunkSize==0)
			nYChunkSize=1;
		
		//printf("%d\n",nYChunkSize);
		i=0;
		do
		{	//iY数据块的起始点y方向坐标
			iY=i*nYChunkSize;

			if( nYChunkSize + iY >nRasterYSize )
				nYChunkSize = nRasterYSize - iY;

			rasterblock pblock;
			
			pblock.ID=i;
			pblock.offx=0;
			pblock.offy=iY;
			pblock.xsize=nRasterXSize;
			pblock.ysize=nYChunkSize;
			pblock.bdone=false;
			Blocks.push_back(pblock);
			i++;
		}while(iY+nYChunkSize<nRasterYSize);

//==================================================================================
//==================栅格数据划分策略============================================
//==================================================================================
		//空闲进程入队列
		for(i=1;i<np;i++)
			IdleProcs.push_back(i);

		//待处理栅格块进栈
		for(i=0;i<Blocks.size();i++)
		{
			unDealBlocks.push_back(i);
			//printf("BID->%ld\toffx->%d\toffy->%d\txsize->%d\tysize->%d\tbDone->%d\n",
					//Blocks.at(i).ID,Blocks.at(i).offx,Blocks.at(i).offy,Blocks.at(i).xsize ,Blocks.at(i).ysize ,Blocks.at(i).bdone);
		}

		int procid,PROCCOUNT;
		
		while(true)
		{
			//如果未处理栅格块栈表已空，处理过程结束
			if(unDealBlocks.empty())
			{
				for(int ccp=1;ccp<np;ccp++)
    				MPI_Send(&pBLOCKINFO,1,MPI_BLOCKINFO,ccp,0,MPI_COMM_WORLD);
				break;
			}
			//如果空闲进程队列已空，主进程进入等待过程
			if(IdleProcs.empty())
			{
				//接收从计算进程发来的执行报告
				MPI_Recv(&pPROCINFO,1,MPI_PROCINFO,MPI_ANY_SOURCE,MPI_ANY_TAG,MPI_COMM_WORLD,&status);
				
				if(! pPROCINFO.bsuccess)
					unDealBlocks.push_back(pPROCINFO.BID );
				
				PROCCOUNT+=1;
				pPROCINFO.PID=PROCCOUNT;

				procinfos.push_back(pPROCINFO);
				IdleProcs.push_back(status.MPI_SOURCE );
			}
			//未处理数据块出栈
			pBLOCKINFO =Blocks.at(unDealBlocks.back());
			unDealBlocks.pop_back();

			//空闲进程出队列
			procid=IdleProcs.front();
			IdleProcs.erase(IdleProcs.begin());

			MPI_Send(&pBLOCKINFO,1,MPI_BLOCKINFO,procid,procid,MPI_COMM_WORLD);

		}
		
		//MPI_Finalize();
	}
	else
	{
		while(true)
		{
			MPI_Recv(&pBLOCKINFO,1,MPI_BLOCKINFO,0,MPI_ANY_TAG,MPI_COMM_WORLD,&status);
			if(status.MPI_TAG==0)
			{
				break;
			}
			//printf("This is cp(%d),Receiving the block infomation:\nBID->%ld\toffx->%d\toffy->%d\txsize->%d\tysize->%d\tbDone->%d\n\n",
			//	cp,pBLOCKINFO.ID,pBLOCKINFO.offx,pBLOCKINFO.offy,pBLOCKINFO.xsize ,pBLOCKINFO.ysize ,pBLOCKINFO.bdone);
			//
		//* ==================================================================== */
		//*      Create a virtual dataset.                                       */
		//* ==================================================================== */
		
			////the data window of the source
			int anSrcWin[4];
			//int nYChunkSize,iY;
			//nYChunkSize = (int)ceil(((GDALDataset*)hDataset)->GetRasterYSize()/(double)np);
			//iY=cp*nYChunkSize;
		
			//if( nYChunkSize + iY >nRasterYSize )
			//	nYChunkSize = nRasterYSize - iY;
		
			anSrcWin[0]=pBLOCKINFO.offx;
			anSrcWin[1]=pBLOCKINFO.offy;
			anSrcWin[2]=pBLOCKINFO.xsize;
			anSrcWin[3]=pBLOCKINFO.ysize;
		
			//the raster size of VRTDataset
			nOXSize = anSrcWin[2];
			nOYSize = anSrcWin[3];
		
		 //--------------------------------------------------------------------
		     // Make a virtual clone.
		 //--------------------------------------------------------------------
			VRTDataset *poVDS;
			poVDS = (VRTDataset *) VRTCreate( nOXSize, nOYSize );
		
			if( nGCPCount == 0 )
				{
					if( pszOutputSRS != NULL )
					{
						poVDS->SetProjection( pszOutputSRS );
					}
					else
					{
						pszProjection = GDALGetProjectionRef( hDataset );
						if( pszProjection != NULL && strlen(pszProjection) > 0 )
							poVDS->SetProjection( pszProjection );
					}
				}
			else
				{
					const char *pszGCPProjection = pszOutputSRS;
		
					if( pszGCPProjection == NULL )
						pszGCPProjection = GDALGetGCPProjection( hDataset );
		
					if( pszGCPProjection == NULL )
						pszGCPProjection = "";
		
					poVDS->SetGCPs( nGCPCount, pasGCPs, pszGCPProjection );
		
					GDALDeinitGCPs( nGCPCount, pasGCPs );
					CPLFree( pasGCPs );
				}

		 //--------------------------------------------------------------------
		     // Transfer generally applicable metadata.
		// --------------------------------------------------------------------
			poVDS->SetMetadata( ((GDALDataset*)hDataset)->GetMetadata());
			AttachMetadata( (GDALDatasetH) poVDS, papszMetadataOptions );
		//
		//// --------------------------------------------------------------------
		//     // Transfer metadata that remains valid if the spatial
		//     // arrangement of the data is unaltered.
		//// --------------------------------------------------------------------
		//
			char **papszMD;
		
			papszMD = ((GDALDataset*)hDataset)->GetMetadata("RPC");
			if( papszMD != NULL )
				poVDS->SetMetadata( papszMD, "RPC" );
		
			papszMD = ((GDALDataset*)hDataset)->GetMetadata("GEOLOCATION");
			if( papszMD != NULL )
				poVDS->SetMetadata( papszMD, "GEOLOCATION" );
		
			int nSrcBandCount = nBandCount;
		
			if (nRGBExpand != 0)
			{
				GDALRasterBand  *poSrcBand;
				poSrcBand = ((GDALDataset *)
							 hDataset)->GetRasterBand(ABS(panBandList[0]));
				if (panBandList[0] < 0)
					poSrcBand = poSrcBand->GetMaskBand();
				GDALColorTable* poColorTable = poSrcBand->GetColorTable();
				if (poColorTable == NULL)
				{
					fprintf(stderr, "Error : band %d has no color table\n", ABS(panBandList[0]));
					GDALClose( hDataset );
					CPLFree( panBandList );
					GDALDestroyDriverManager();
					CSLDestroy( argv );
					CSLDestroy( papszCreateOptions );
					exit( 1 );
				}
		
				 //Check that the color table only contains gray levels
				// when using -expand gray
				if (nRGBExpand == 1)
				{
					int nColorCount = poColorTable->GetColorEntryCount();
					int nColor;
					for( nColor = 0; nColor < nColorCount; nColor++ )
					{
						const GDALColorEntry* poEntry = poColorTable->GetColorEntry(nColor);
						if (poEntry->c1 != poEntry->c2 || poEntry->c1 != poEntry->c2)
						{
							fprintf(stderr, "Warning : color table contains non gray levels colors\n");
							break;
						}
					}
				}
		
				if (nBandCount == 1)
					nBandCount = nRGBExpand;
				else if (nBandCount == 2 && (nRGBExpand == 3 || nRGBExpand == 4))
					nBandCount = nRGBExpand;
				else
				{
					fprintf(stderr, "Error : invalid use of -expand option.\n");
					exit( 1 );
				}
			}
		
			int bFilterOutStatsMetadata =
				(bScale || bUnscale || nRGBExpand != 0);
		
		 //====================================================================
		  //    Process all bands.
		 //====================================================================
			for( i = 0; i < nBandCount; i++ )
			{
				VRTSourcedRasterBand   *poVRTBand;
				GDALRasterBand  *poSrcBand;
				GDALDataType    eBandType;
				int nComponent = 0;
		
				int nSrcBand;
				if (nRGBExpand != 0)
				{
					if (nSrcBandCount == 2 && nRGBExpand == 4 && i == 3)
						nSrcBand = panBandList[1];
					else
					{
						nSrcBand = panBandList[0];
						nComponent = i + 1;
					}
				}
				else
					nSrcBand = panBandList[i];
		
				poSrcBand = ((GDALDataset *) hDataset)->GetRasterBand(ABS(nSrcBand));
		
		 //--------------------------------------------------------------------
		  //    Select output data type to match source.
		 //--------------------------------------------------------------------
				if( eOutputType == GDT_Unknown )
					eBandType = poSrcBand->GetRasterDataType();
				else
					eBandType = eOutputType;
		
		// --------------------------------------------------------------------
		 //     Create this band.
		// --------------------------------------------------------------------
				poVDS->AddBand( eBandType, NULL );
				poVRTBand = (VRTSourcedRasterBand *) poVDS->GetRasterBand( i+1 );
				if (nSrcBand < 0)
				{
					poVRTBand->AddMaskBandSource(poSrcBand);
					continue;
				}
		
		 //--------------------------------------------------------------------
		 //     Do we need to collect scaling information?
		 //--------------------------------------------------------------------
				double dfScale=1.0, dfOffset=0.0;
		
				if( bScale && !bHaveScaleSrc )
				{
					double	adfCMinMax[2];
					GDALComputeRasterMinMax( poSrcBand, TRUE, adfCMinMax );
					dfScaleSrcMin = adfCMinMax[0];
					dfScaleSrcMax = adfCMinMax[1];
				}
		
				if( bScale )
				{
					if( dfScaleSrcMax == dfScaleSrcMin )
						dfScaleSrcMax += 0.1;
					if( dfScaleDstMax == dfScaleDstMin )
						dfScaleDstMax += 0.1;
		
					dfScale = (dfScaleDstMax - dfScaleDstMin)
						/ (dfScaleSrcMax - dfScaleSrcMin);
					dfOffset = -1 * dfScaleSrcMin * dfScale + dfScaleDstMin;
				}
		
				if( bUnscale )
				{
					dfScale = poSrcBand->GetScale();
					dfOffset = poSrcBand->GetOffset();
				}
		
		 //--------------------------------------------------------------------
		 //     Create a simple or complex data source depending on the
		 //     translation type required.
		 //--------------------------------------------------------------------
				if( bUnscale || bScale || (nRGBExpand != 0 && i < nRGBExpand) )
				{
					poVRTBand->AddComplexSource( poSrcBand,
												 anSrcWin[0], anSrcWin[1],
												 anSrcWin[2], anSrcWin[3],
												 0, 0, nOXSize, nOYSize,
												 dfOffset, dfScale,
												 VRT_NODATA_UNSET,
												 nComponent );
				}
				else
					poVRTBand->AddSimpleSource( poSrcBand,
												anSrcWin[0], anSrcWin[1],
												anSrcWin[2], anSrcWin[3],
												0, 0, nOXSize, nOYSize );
		
		// --------------------------------------------------------------------
		 //     In case of color table translate, we only set the color
		 //     interpretation other info copied by CopyBandInfo are
		 //     not relevant in RGB expansion.
		 //--------------------------------------------------------------------
				if (nRGBExpand == 1)
				{
					poVRTBand->SetColorInterpretation( GCI_GrayIndex );
				}
				else if (nRGBExpand != 0 && i < nRGBExpand)
				{
					poVRTBand->SetColorInterpretation( (GDALColorInterp) (GCI_RedBand + i) );
				}
		
		// --------------------------------------------------------------------
		    //  copy over some other information of interest.
		// --------------------------------------------------------------------
				else
				{
					CopyBandInfo( poSrcBand, poVRTBand,
								  !bStats && !bFilterOutStatsMetadata,
								  !bUnscale,
								  !bSetNoData && !bUnsetNoData );
				}
		
		 //--------------------------------------------------------------------
		 //     Set a forcable nodata value?
		 //--------------------------------------------------------------------
				if( bSetNoData )
				{
					double dfVal = dfNoDataReal;
					int bClamped = FALSE, bRounded = FALSE;
		
		#define CLAMP(val,type,minval,maxval) \
			do { if (val < minval) { bClamped = TRUE; val = minval; } \
			else if (val > maxval) { bClamped = TRUE; val = maxval; } \
			else if (val != (type)val) { bRounded = TRUE; val = (type)(val + 0.5); } } \
			while(0)
		
					switch(eBandType)
					{
						case GDT_Byte:
							CLAMP(dfVal, GByte, 0.0, 255.0);
							break;
						case GDT_Int16:
							CLAMP(dfVal, GInt16, -32768.0, 32767.0);
							break;
						case GDT_UInt16:
							CLAMP(dfVal, GUInt16, 0.0, 65535.0);
							break;
						case GDT_Int32:
							CLAMP(dfVal, GInt32, -2147483648.0, 2147483647.0);
							break;
						case GDT_UInt32:
							CLAMP(dfVal, GUInt32, 0.0, 4294967295.0);
							break;
						default:
							break;
					}
		
					if (bClamped)
					{
						printf( "for band %d, nodata value has been clamped "
							   "to %.0f, the original value being out of range.\n",
							   i + 1, dfVal);
					}
					else if(bRounded)
					{
						printf("for band %d, nodata value has been rounded "
							   "to %.0f, %s being an integer datatype.\n",
							   i + 1, dfVal,
							   GDALGetDataTypeName(eBandType));
					}
		
					poVRTBand->SetNoDataValue( dfVal );
				}
		
				if (eMaskMode == MASK_AUTO &&
					(GDALGetMaskFlags(GDALGetRasterBand(hDataset, 1)) & GMF_PER_DATASET) == 0 &&
					(poSrcBand->GetMaskFlags() & (GMF_ALL_VALID | GMF_NODATA)) == 0)
				{
					if (poVRTBand->CreateMaskBand(poSrcBand->GetMaskFlags()) == CE_None)
					{
						VRTSourcedRasterBand* hMaskVRTBand =
							(VRTSourcedRasterBand*)poVRTBand->GetMaskBand();
		
						hMaskVRTBand->AddMaskBandSource(poSrcBand,
												anSrcWin[0], anSrcWin[1],
												anSrcWin[2], anSrcWin[3],
												0, 0, nOXSize, nOYSize );
					}
				}
			}
		//
		//	if (eMaskMode == MASK_USER)
		//	{
		//		GDALRasterBand *poSrcBand =
		//			(GDALRasterBand*)GDALGetRasterBand(hDataset, ABS(nMaskBand));
		//		if (poSrcBand && poVDS->CreateMaskBand(GMF_PER_DATASET) == CE_None)
		//		{
		//			VRTSourcedRasterBand* hMaskVRTBand = (VRTSourcedRasterBand*)
		//				GDALGetMaskBand(GDALGetRasterBand((GDALDatasetH)poVDS, 1));
		//			if (nMaskBand > 0)
		//				hMaskVRTBand->AddSimpleSource(poSrcBand,
		//										anSrcWin[0], anSrcWin[1],
		//										anSrcWin[2], anSrcWin[3],
		//										0, 0, nOXSize, nOYSize );
		//			else
		//				hMaskVRTBand->AddMaskBandSource(poSrcBand,
		//										anSrcWin[0], anSrcWin[1],
		//										anSrcWin[2], anSrcWin[3],
		//										0, 0, nOXSize, nOYSize );
		//		}
		//	}
		//	else if (eMaskMode == MASK_AUTO && nSrcBandCount > 0 &&
		//		GDALGetMaskFlags(GDALGetRasterBand(hDataset, 1)) == GMF_PER_DATASET)
		//	{
		//		if (poVDS->CreateMaskBand(GMF_PER_DATASET) == CE_None)
		//		{
		//			VRTSourcedRasterBand* hMaskVRTBand = (VRTSourcedRasterBand*)
		//				GDALGetMaskBand(GDALGetRasterBand((GDALDatasetH)poVDS, 1));
		//			hMaskVRTBand->AddMaskBandSource((GDALRasterBand*)GDALGetRasterBand(hDataset, 1),
		//										anSrcWin[0], anSrcWin[1],
		//										anSrcWin[2], anSrcWin[3],
		//										0, 0, nOXSize, nOYSize );
		//		}
		//	}
		//
		// //--------------------------------------------------------------------
		//   //   Compute statistics if required.
		//// --------------------------------------------------------------------
			if (bStats)
			{
				for( i = 0; i < poVDS->GetRasterCount(); i++ )
				{
					double dfMin, dfMax, dfMean, dfStdDev;
					poVDS->GetRasterBand(i+1)->ComputeStatistics( bApproxStats,
							&dfMin, &dfMax, &dfMean, &dfStdDev, GDALDummyProgress, NULL );
				}
			}
		
		 //--------------------------------------------------------------------
		    //  Write to the output file using CopyCreate().
		// --------------------------------------------------------------------
			int lenDest=strlen(pszDest);
			char * filename_cp=(char*) malloc (sizeof(char)*(lenDest+10));
		
			strcpy(filename_cp,pszDest);
		
			if(np>1)
			{
				char post_fix[10];
				int p;
		
				for( p=lenDest-1;p>=0;p--)
				{
					if(pszDest[p]=='.')
						break;
				}
		
				if(p==0)
					post_fix[0]='\0';
				else
				{
					strcpy(post_fix,pszDest+p);
					for(;p<lenDest-1;p++)
						filename_cp[p]='\0';
				}
		
				char tostr[10];
				sprintf(tostr,"%d",cp);
		
				strcat(filename_cp,tostr);
				strcat(filename_cp,post_fix);
			}
		
			hOutDS = GDALCreateCopy( hDriver, filename_cp, (GDALDatasetH) poVDS,
								 bStrict, papszCreateOptions,
								 NULL, NULL );
			if( hOutDS != NULL )
			{
				if(np>1)
				{
					hDistDataset = GDALOpenShared( pszDest, GA_Update );
					CopyWholeRaster(hOutDS,hDistDataset,anSrcWin[0],anSrcWin[1]);
					
				}
	
				int bHasGotErr = FALSE;
				CPLErrorReset();
				GDALFlushCache( hOutDS );
				if (CPLGetLastErrorType() != CE_None)
					bHasGotErr = TRUE;
	
				GDALClose( hDistDataset );
	
				GDALClose( hOutDS );
				if (bHasGotErr)
					hOutDS = NULL;

				GDALDeleteDataset(hDriver,filename_cp);
			}
			
		
			time2=MPI_Wtime();
			//printf("This is process No.< %d >, and I've done my job in < %f > seconds!\n",cp,time2-time1);
		


			GDALClose( (GDALDatasetH) poVDS );
			//CPLFree( filename_cp );

			pPROCINFO.BID=pBLOCKINFO.ID;
			pPROCINFO.RID=cp;
			pPROCINFO.start=time1;
			pPROCINFO.end=time2;
			pPROCINFO.bsuccess=true;
			MPI_Send(&pPROCINFO,1,MPI_PROCINFO,0,cp,MPI_COMM_WORLD);
			
		}
	}

	if(cp==0)
	{
		time2=MPI_Wtime();
		printf("Done the job in < %f > seconds!\n",time2-time1);
		
		ofstream logs;
		char *filename="/share/rdata/prj2/translate_test.log";
		
		logs.open(filename,ios::app);
		logs<<np<<"\t"<<time2-time1<<endl;
		logs.close();
	}
	GDALClose( hDataset );
	CPLFree( panBandList );
	CPLFree( pszOutputSRS );

	if( !bSubCall )
		GDALDumpOpenDatasets( stderr );

	GDALDestroyDriverManager();
	CSLDestroy( argv );
	CSLDestroy( papszCreateOptions );

	MPI_Finalize();
	return hOutDS == NULL;
}


/************************************************************************/
/*                            ArgIsNumeric()                            */
/************************************************************************/



int ArgIsNumeric( const char *pszArg )

{
    if( pszArg[0] == '-' )
        pszArg++;

    if( *pszArg == '\0' )
        return FALSE;

    while( *pszArg != '\0' )
    {
        if( (*pszArg < '0' || *pszArg > '9') && *pszArg != '.' )
            return FALSE;
        pszArg++;
    }

    return TRUE;
}

/************************************************************************/
/*                           AttachMetadata()                           */
/************************************************************************/

static void AttachMetadata( GDALDatasetH hDS, char **papszMetadataOptions )

{
    int nCount = CSLCount(papszMetadataOptions);
    int i;

    for( i = 0; i < nCount; i++ )
    {
        char    *pszKey = NULL;
        const char *pszValue;

        pszValue = CPLParseNameValue( papszMetadataOptions[i], &pszKey );
        GDALSetMetadataItem(hDS,pszKey,pszValue,NULL);
        CPLFree( pszKey );
    }

    CSLDestroy( papszMetadataOptions );
}

/************************************************************************/
/*                           CopyBandInfo()                            */
/************************************************************************/

/* A bit of a clone of VRTRasterBand::CopyCommonInfoFrom(), but we need */
/* more and more custom behaviour in the context of gdal_translate ... */

static void CopyBandInfo( GDALRasterBand * poSrcBand, GDALRasterBand * poDstBand,
                          int bCanCopyStatsMetadata, int bCopyScale, int bCopyNoData )

{
    int bSuccess;
    double dfNoData;

    if (bCanCopyStatsMetadata)
    {
        poDstBand->SetMetadata( poSrcBand->GetMetadata() );
    }
    else
    {
        char** papszMetadata = poSrcBand->GetMetadata();
        char** papszMetadataNew = NULL;
        for( int i = 0; papszMetadata != NULL && papszMetadata[i] != NULL; i++ )
        {
            if (strncmp(papszMetadata[i], "STATISTICS_", 11) != 0)
                papszMetadataNew = CSLAddString(papszMetadataNew, papszMetadata[i]);
        }
        poDstBand->SetMetadata( papszMetadataNew );
        CSLDestroy(papszMetadataNew);
    }

    poDstBand->SetColorTable( poSrcBand->GetColorTable() );
    poDstBand->SetColorInterpretation(poSrcBand->GetColorInterpretation());
    if( strlen(poSrcBand->GetDescription()) > 0 )
        poDstBand->SetDescription( poSrcBand->GetDescription() );

    if (bCopyNoData)
    {
        dfNoData = poSrcBand->GetNoDataValue( &bSuccess );
        if( bSuccess )
            poDstBand->SetNoDataValue( dfNoData );
    }

    if (bCopyScale)
    {
        poDstBand->SetOffset( poSrcBand->GetOffset() );
        poDstBand->SetScale( poSrcBand->GetScale() );
    }

    poDstBand->SetCategoryNames( poSrcBand->GetCategoryNames() );
    if( !EQUAL(poSrcBand->GetUnitType(),"") )
        poDstBand->SetUnitType( poSrcBand->GetUnitType() );
}


/************************************************************************/
/*                                main()                                */
/************************************************************************/

int main( int argc, char ** argv )

{
	//argc=5;

	//argv[0]="C:\\Documents and Settings\\sk\\My Documents\\Visual Studio 2008\\Projects\\gdal_translate\\Debug\\gdal_translate.exe";
	//argv[1]="-of";
	//argv[2]="GTiff";
	//argv[3]="c:\\data\\img\\km.img";
	//argv[4]="c:\\data\\img\\km.tif";

	int result=0;

	result= ProxyMain( argc, argv );

	return result;


}
