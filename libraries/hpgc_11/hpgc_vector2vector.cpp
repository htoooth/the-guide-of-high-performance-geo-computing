/******************************************************************************
 * $Id: ogr2ogr.cpp 22027 2011-03-25 19:28:44Z rouault $
 *
 * Project:  OpenGIS Simple Features Reference Implementation
 * Purpose:  Simple client for translating between formats.
 * Author:   Frank Warmerdam, warmerdam@pobox.com
 *
 ******************************************************************************
 * Copyright (c) 1999, Frank Warmerdam
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

#include "ogrsf_frmts.h"
#include "ogr_p.h"
#include "cpl_conv.h"
#include "cpl_string.h"
#include "ogr_api.h"
#include "gdal.h"
#include "mpi.h"
#include <math.h>

#include <vector>
#include <stdio.h>
#include <fstream>
using namespace std;

#define master 0
//#define MSG_PANMAP 1
#define MSG_FINISHED_TRANSLATING 0
#define MSG_FINISHED_WRITING 1
#define MSG_STARTING_WRITING 2

CPL_CVSID("$Id: ogr2ogr.cpp 22027 2011-03-25 19:28:44Z rouault $");

static int bSkipFailures = FALSE;
//static int nGroupTransactions = 200;
static int bPreserveFID = FALSE;
static int nFIDToFetch = OGRNullFID;
static 	int nBlock=1;//定义数据块

static void Usage(int bShort = TRUE);

static int TranslateLayer( OGRDataSource *poSrcDS, 
                           OGRLayer * poSrcLayer,
                           OGRDataSource *poDstDS,
                           char ** papszLSCO,
                           const char *pszNewLayerName,
                           char **papszSelFields,
                           int bAppend, int eGType,
                           int bOverwrite,
                           char** papszFieldTypesToString,
                           //long nCountLayerFeatures,
                           OGRGeometry* poClipSrc,
                           const char* pszWHERE,
						   const char *pszDestDataSource,
						   const char  *pszSFormat
						   );


void exitMPI(int code)
{
	printf("\n");
	MPI_Finalize();
	exit(0);
}
/************************************************************************/
/*                            IsNumber()                               */
/************************************************************************/

static int IsNumber(const char* pszStr)
{
    if (*pszStr == '-' || *pszStr == '+')
        pszStr ++;
    if (*pszStr == '.')
        pszStr ++;
    return (*pszStr >= '0' && *pszStr <= '9');
}

/************************************************************************/
/*                           LoadGeometry()                             */
/************************************************************************/

static OGRGeometry* LoadGeometry( const char* pszDS,
                                  const char* pszSQL,
                                  const char* pszLyr,
                                  const char* pszWhere)
{
    OGRDataSource       *poDS;
    OGRLayer            *poLyr;
    OGRFeature          *poFeat;
    OGRGeometry         *poGeom = NULL;
        
    poDS = OGRSFDriverRegistrar::Open( pszDS, FALSE );
    if (poDS == NULL)
        return NULL;

    if (pszSQL != NULL)
        poLyr = poDS->ExecuteSQL( pszSQL, NULL, NULL ); 
    else if (pszLyr != NULL)
        poLyr = poDS->GetLayerByName(pszLyr);
    else
        poLyr = poDS->GetLayer(0);
        
    if (poLyr == NULL)
    {
        fprintf( stderr, "Failed to identify source layer from datasource.\n" );
        OGRDataSource::DestroyDataSource(poDS);
        return NULL;
    }
    
    if (pszWhere)
        poLyr->SetAttributeFilter(pszWhere);
        
    while ((poFeat = poLyr->GetNextFeature()) != NULL)
    {
        OGRGeometry* poSrcGeom = poFeat->GetGeometryRef();
        if (poSrcGeom)
        {
            OGRwkbGeometryType eType = wkbFlatten( poSrcGeom->getGeometryType() );
            
            if (poGeom == NULL)
                poGeom = OGRGeometryFactory::createGeometry( wkbMultiPolygon );

            if( eType == wkbPolygon )
                ((OGRGeometryCollection*)poGeom)->addGeometry( poSrcGeom );
            else if( eType == wkbMultiPolygon )
            {
                int iGeom;
                int nGeomCount = OGR_G_GetGeometryCount( (OGRGeometryH)poSrcGeom );

                for( iGeom = 0; iGeom < nGeomCount; iGeom++ )
                {
                    ((OGRGeometryCollection*)poGeom)->addGeometry(
                                ((OGRGeometryCollection*)poSrcGeom)->getGeometryRef(iGeom) );
                }
            }
            else
            {
                fprintf( stderr, "ERROR: Geometry not of polygon type.\n" );
                OGRGeometryFactory::destroyGeometry(poGeom);
                OGRFeature::DestroyFeature(poFeat);
                if( pszSQL != NULL )
                    poDS->ReleaseResultSet( poLyr );
                OGRDataSource::DestroyDataSource(poDS);
                return NULL;
            }
        }
    
        OGRFeature::DestroyFeature(poFeat);
    }
    
    if( pszSQL != NULL )
        poDS->ReleaseResultSet( poLyr );
    OGRDataSource::DestroyDataSource(poDS);
    
    return poGeom;
}


/************************************************************************/
/*                     OGRSplitListFieldLayer                           */
/************************************************************************/

typedef struct
{
    int          iSrcIndex;
    OGRFieldType eType;
    int          nMaxOccurences;
    int          nWidth;
} ListFieldDesc;

class OGRSplitListFieldLayer : public OGRLayer
{
    OGRLayer                    *poSrcLayer;
    OGRFeatureDefn              *poFeatureDefn;
    ListFieldDesc               *pasListFields;
    int                          nListFieldCount;
    int                          nMaxSplitListSubFields;

    OGRFeature                  *TranslateFeature(OGRFeature* poSrcFeature);

  public:
                                 OGRSplitListFieldLayer(OGRLayer* poSrcLayer,
                                                        int nMaxSplitListSubFields);
                                ~OGRSplitListFieldLayer();

    int                          BuildLayerDefn(GDALProgressFunc pfnProgress,
                                                void *pProgressArg);

    virtual OGRFeature          *GetNextFeature();
    virtual OGRFeature          *GetFeature(long nFID);
    virtual OGRFeatureDefn      *GetLayerDefn();

    virtual void                 ResetReading() { poSrcLayer->ResetReading(); }
    virtual int                  TestCapability(const char*) { return FALSE; }

    virtual int                  GetFeatureCount()
    {
        return poSrcLayer->GetFeatureCount();
    }

    virtual OGRSpatialReference *GetSpatialRef()
    {
        return poSrcLayer->GetSpatialRef();
    }

    virtual OGRGeometry         *GetSpatialFilter()
    {
        return poSrcLayer->GetSpatialFilter();
    }

    virtual OGRStyleTable       *GetStyleTable()
    {
        return poSrcLayer->GetStyleTable();
    }

    virtual void                 SetSpatialFilter( OGRGeometry *poGeom )
    {
        poSrcLayer->SetSpatialFilter(poGeom);
    }

    virtual void                 SetSpatialFilterRect( double dfMinX, double dfMinY,
                                                       double dfMaxX, double dfMaxY )
    {
        poSrcLayer->SetSpatialFilterRect(dfMinX, dfMinY, dfMaxX, dfMaxY);
    }

    virtual OGRErr               SetAttributeFilter( const char *pszFilter )
    {
        return poSrcLayer->SetAttributeFilter(pszFilter);
    }
};

/************************************************************************/
/*                    OGRSplitListFieldLayer()                          */
/************************************************************************/

OGRSplitListFieldLayer::OGRSplitListFieldLayer(OGRLayer* poSrcLayer,
                                               int nMaxSplitListSubFields)
{
    this->poSrcLayer = poSrcLayer;
    if (nMaxSplitListSubFields < 0)
        nMaxSplitListSubFields = INT_MAX;
    this->nMaxSplitListSubFields = nMaxSplitListSubFields;
    poFeatureDefn = NULL;
    pasListFields = NULL;
    nListFieldCount = 0;
}

/************************************************************************/
/*                   ~OGRSplitListFieldLayer()                          */
/************************************************************************/

OGRSplitListFieldLayer::~OGRSplitListFieldLayer()
{
    if( poFeatureDefn )
        poFeatureDefn->Release();

    CPLFree(pasListFields);
}

/************************************************************************/
/*                       BuildLayerDefn()                               */
/************************************************************************/

int  OGRSplitListFieldLayer::BuildLayerDefn(GDALProgressFunc pfnProgress,
                                            void *pProgressArg)
{
    CPLAssert(poFeatureDefn == NULL);
    
    OGRFeatureDefn* poSrcFieldDefn = poSrcLayer->GetLayerDefn();
    
    int nSrcFields = poSrcFieldDefn->GetFieldCount();
    pasListFields =
            (ListFieldDesc*)CPLCalloc(sizeof(ListFieldDesc), nSrcFields);
    nListFieldCount = 0;
    int i;
    
    /* Establish the list of fields of list type */
    for(i=0;i<nSrcFields;i++)
    {
        OGRFieldType eType = poSrcFieldDefn->GetFieldDefn(i)->GetType();
        if (eType == OFTIntegerList ||
            eType == OFTRealList ||
            eType == OFTStringList)
        {
            pasListFields[nListFieldCount].iSrcIndex = i;
            pasListFields[nListFieldCount].eType = eType;
            if (nMaxSplitListSubFields == 1)
                pasListFields[nListFieldCount].nMaxOccurences = 1;
            nListFieldCount++;
        }
    }

    if (nListFieldCount == 0)
        return FALSE;

    /* No need for full scan if the limit is 1. We just to have to create */
    /* one and a single one field */
    if (nMaxSplitListSubFields != 1)
    {
        poSrcLayer->ResetReading();
        OGRFeature* poSrcFeature;

        int nFeatureCount = 0;
        if (poSrcLayer->TestCapability(OLCFastFeatureCount))
            nFeatureCount = poSrcLayer->GetFeatureCount();
        int nFeatureIndex = 0;

        /* Scan the whole layer to compute the maximum number of */
        /* items for each field of list type */
        while( (poSrcFeature = poSrcLayer->GetNextFeature()) != NULL )
        {
            for(i=0;i<nListFieldCount;i++)
            {
                int nCount = 0;
                OGRField* psField =
                        poSrcFeature->GetRawFieldRef(pasListFields[i].iSrcIndex);
                switch(pasListFields[i].eType)
                {
                    case OFTIntegerList:
                        nCount = psField->IntegerList.nCount;
                        break;
                    case OFTRealList:
                        nCount = psField->RealList.nCount;
                        break;
                    case OFTStringList:
                    {
                        nCount = psField->StringList.nCount;
                        char** paList = psField->StringList.paList;
                        int j;
                        for(j=0;j<nCount;j++)
                        {
                            int nWidth = strlen(paList[j]);
                            if (nWidth > pasListFields[i].nWidth)
                                pasListFields[i].nWidth = nWidth;
                        }
                        break;
                    }
                    default:
                        CPLAssert(0);
                        break;
                }
                if (nCount > pasListFields[i].nMaxOccurences)
                {
                    if (nCount > nMaxSplitListSubFields)
                        nCount = nMaxSplitListSubFields;
                    pasListFields[i].nMaxOccurences = nCount;
                }
            }
            OGRFeature::DestroyFeature(poSrcFeature);

            nFeatureIndex ++;
            if (pfnProgress != NULL && nFeatureCount != 0)
                pfnProgress(nFeatureIndex * 1.0 / nFeatureCount, "", pProgressArg);
        }
    }

    /* Now let's build the target feature definition */

    poFeatureDefn =
            OGRFeatureDefn::CreateFeatureDefn( poSrcFieldDefn->GetName() );
    poFeatureDefn->Reference();
    poFeatureDefn->SetGeomType( poSrcFieldDefn->GetGeomType() );

    int iListField = 0;
    for(i=0;i<nSrcFields;i++)
    {
        OGRFieldType eType = poSrcFieldDefn->GetFieldDefn(i)->GetType();
        if (eType == OFTIntegerList ||
            eType == OFTRealList ||
            eType == OFTStringList)
        {
            int nMaxOccurences = pasListFields[iListField].nMaxOccurences;
            int nWidth = pasListFields[iListField].nWidth;
            iListField ++;
            int j;
            if (nMaxOccurences == 1)
            {
                OGRFieldDefn oFieldDefn(poSrcFieldDefn->GetFieldDefn(i)->GetNameRef(),
                                            (eType == OFTIntegerList) ? OFTInteger :
                                            (eType == OFTRealList) ?    OFTReal :
                                                                        OFTString);
                poFeatureDefn->AddFieldDefn(&oFieldDefn);
            }
            else
            {
                for(j=0;j<nMaxOccurences;j++)
                {
                    CPLString osFieldName;
                    osFieldName.Printf("%s%d",
                        poSrcFieldDefn->GetFieldDefn(i)->GetNameRef(), j+1);
                    OGRFieldDefn oFieldDefn(osFieldName.c_str(),
                                            (eType == OFTIntegerList) ? OFTInteger :
                                            (eType == OFTRealList) ?    OFTReal :
                                                                        OFTString);
                    oFieldDefn.SetWidth(nWidth);
                    poFeatureDefn->AddFieldDefn(&oFieldDefn);
                }
            }
        }
        else
        {
            poFeatureDefn->AddFieldDefn(poSrcFieldDefn->GetFieldDefn(i));
        }
    }

    return TRUE;
}


/************************************************************************/
/*                       TranslateFeature()                             */
/************************************************************************/

OGRFeature *OGRSplitListFieldLayer::TranslateFeature(OGRFeature* poSrcFeature)
{
    if (poSrcFeature == NULL)
        return NULL;
    if (poFeatureDefn == NULL)
        return poSrcFeature;

    OGRFeature* poFeature = OGRFeature::CreateFeature(poFeatureDefn);
    poFeature->SetFID(poSrcFeature->GetFID());
    poFeature->SetGeometryDirectly(poSrcFeature->StealGeometry());
    poFeature->SetStyleString(poFeature->GetStyleString());

    OGRFeatureDefn* poSrcFieldDefn = poSrcLayer->GetLayerDefn();
    int nSrcFields = poSrcFeature->GetFieldCount();
    int iSrcField;
    int iDstField = 0;
    int iListField = 0;
    int j;
    for(iSrcField=0;iSrcField<nSrcFields;iSrcField++)
    {
        OGRFieldType eType = poSrcFieldDefn->GetFieldDefn(iSrcField)->GetType();
        OGRField* psField = poSrcFeature->GetRawFieldRef(iSrcField);
        switch(eType)
        {
            case OFTIntegerList:
            {
                int nCount = psField->IntegerList.nCount;
                if (nCount > nMaxSplitListSubFields)
                    nCount = nMaxSplitListSubFields;
                int* paList = psField->IntegerList.paList;
                for(j=0;j<nCount;j++)
                    poFeature->SetField(iDstField + j, paList[j]);
                iDstField += pasListFields[iListField].nMaxOccurences;
                iListField++;
                break;
            }
            case OFTRealList:
            {
                int nCount = psField->RealList.nCount;
                if (nCount > nMaxSplitListSubFields)
                    nCount = nMaxSplitListSubFields;
                double* paList = psField->RealList.paList;
                for(j=0;j<nCount;j++)
                    poFeature->SetField(iDstField + j, paList[j]);
                iDstField += pasListFields[iListField].nMaxOccurences;
                iListField++;
                break;
            }
            case OFTStringList:
            {
                int nCount = psField->StringList.nCount;
                if (nCount > nMaxSplitListSubFields)
                    nCount = nMaxSplitListSubFields;
                char** paList = psField->StringList.paList;
                for(j=0;j<nCount;j++)
                    poFeature->SetField(iDstField + j, paList[j]);
                iDstField += pasListFields[iListField].nMaxOccurences;
                iListField++;
                break;
            }
            default:
                poFeature->SetField(iDstField, psField);
                iDstField ++;
                break;
        }
    }

    OGRFeature::DestroyFeature(poSrcFeature);

    return poFeature;
}

/************************************************************************/
/*                       GetNextFeature()                               */
/************************************************************************/

OGRFeature *OGRSplitListFieldLayer::GetNextFeature()
{
    return TranslateFeature(poSrcLayer->GetNextFeature());
}

/************************************************************************/
/*                           GetFeature()                               */
/************************************************************************/

OGRFeature *OGRSplitListFieldLayer::GetFeature(long nFID)
{
    return TranslateFeature(poSrcLayer->GetFeature(nFID));
}

/************************************************************************/
/*                        GetLayerDefn()                                */
/************************************************************************/

OGRFeatureDefn* OGRSplitListFieldLayer::GetLayerDefn()
{
    if (poFeatureDefn == NULL)
        return poSrcLayer->GetLayerDefn();
    return poFeatureDefn;
}

/************************************************************************/
/*                                main()                                */
/************************************************************************/

int main( int nArgc, char ** papszArgv )

{
    const char  *pszFormat = "ESRI Shapefile";
    const char  *pszDataSource = NULL;
    const char  *pszDestDataSource = NULL;
    char        **papszLayers = NULL;
    char        **papszDSCO = NULL, **papszLCO = NULL;
    //int         bTransform = FALSE;
    int         bAppend = FALSE, bUpdate = FALSE, bOverwrite = FALSE;
    //const char  *pszOutputSRSDef = NULL;
    //const char  *pszSourceSRSDef = NULL;
    //OGRSpatialReference *poOutputSRS = NULL;
    //int         bNullifyOutputSRS = FALSE;
    //OGRSpatialReference *poSourceSRS = NULL;
    char        *pszNewLayerName = NULL;
    const char  *pszWHERE = NULL;
    OGRGeometry *poSpatialFilter = NULL;
    const char  *pszSelect;
    char        **papszSelFields = NULL;
    const char  *pszSQLStatement = NULL;
    const char  *pszDialect = NULL;
    int         eGType = -2;
    //double       dfMaxSegmentLength = 0;
    char        **papszFieldTypesToString = NULL;
    //int          bDisplayProgress = FALSE;
    //GDALProgressFunc pfnProgress = NULL;
    //void        *pProgressArg = NULL;
    //int          bWrapDateline = FALSE;
    int          bClipSrc = FALSE;
    OGRGeometry* poClipSrc = NULL;
    const char  *pszClipSrcDS = NULL;
    const char  *pszClipSrcSQL = NULL;
    const char  *pszClipSrcLayer = NULL;
    const char  *pszClipSrcWhere = NULL;


    //OGRGeometry *poClipDst = NULL;
    //const char  *pszClipDstDS = NULL;
    //const char  *pszClipDstSQL = NULL;
    //const char  *pszClipDstLayer = NULL;
    //const char  *pszClipDstWhere = NULL;
    //int          bSplitListFields = FALSE;
    //int          nMaxSplitListSubFields = -1;
    //int          bExplodeCollections = FALSE;
    //const char  *pszZField = NULL;
	double time1,time2;

    /* Check strict compilation and runtime library version as we use C++ API */
    if (! GDAL_CHECK_VERSION(papszArgv[0]))
        exitMPI(1);

	
	

/* -------------------------------------------------------------------- */
/*      Register format(s).                                             */
/* -------------------------------------------------------------------- */
    OGRRegisterAll();

/* parallel program */
	int cp, np;
	MPI_Init( &nArgc, &papszArgv);
	MPI_Comm_rank(MPI_COMM_WORLD, &cp);
	MPI_Comm_size(MPI_COMM_WORLD, &np);

	/*check the time at start point*/
	time1=MPI_Wtime();
	
	//papszArgv[0]="ogr_ogr";
	//papszArgv[1]="-f";
	//papszArgv[2]="MapInfo File";
	//papszArgv[3]="d:\\data\\b.tab";
	//papszArgv[4]="d:\\data\\line.shp";
	//nArgc=5;

/* -------------------------------------------------------------------- */
/*      Processing command line arguments.                              */
/* -------------------------------------------------------------------- */
    nArgc = OGRGeneralCmdLineProcessor( nArgc, &papszArgv, 0 );
    
    if( nArgc < 1 )
       exitMPI( -nArgc );

    for( int iArg = 1; iArg < nArgc; iArg++ )
    {
        if( EQUAL(papszArgv[iArg], "--utility_version") )
        {
            printf("%s was compiled against GDAL %s and is running against GDAL %s\n",
                   papszArgv[0], GDAL_RELEASE_NAME, GDALVersionInfo("RELEASE_NAME"));
            return 0;
        }
        else if ( EQUAL(papszArgv[iArg], "--long-usage") )
        {
            Usage(FALSE);
        }
        else if( EQUAL(papszArgv[iArg],"-f") && iArg < nArgc-1 )
        {
            pszFormat = papszArgv[++iArg];
			if(!EQUAL(pszFormat , "ESRI Shapefile"))
			{
				printf("Sorry,Only ESRI Shapefile format is supported by now!\nExit...\n");
				Usage(FALSE);
			}
        }
        else if( EQUAL(papszArgv[iArg],"-dsco") && iArg < nArgc-1 )
        {
            papszDSCO = CSLAddString(papszDSCO, papszArgv[++iArg] );
        }
        else if( EQUAL(papszArgv[iArg],"-lco") && iArg < nArgc-1 )
        {
            papszLCO = CSLAddString(papszLCO, papszArgv[++iArg] );
        }
        else if( EQUAL(papszArgv[iArg],"-preserve_fid") )
        {
            bPreserveFID = TRUE;
        }
        else if( EQUALN(papszArgv[iArg],"-skip",5) )
        {
            bSkipFailures = TRUE;
            //nGroupTransactions = 1; /* #2409 */
        }
        else if( EQUAL(papszArgv[iArg],"-append") )
        {
            bAppend = TRUE;
            bUpdate = TRUE;
        }
        else if( EQUAL(papszArgv[iArg],"-overwrite") )
        {
            bOverwrite = TRUE;
            bUpdate = TRUE;
        }
        else if( EQUAL(papszArgv[iArg],"-update") )
        {
            bUpdate = TRUE;
        }
		else if (EQUAL(papszArgv[iArg],"-nb"))
		{
			nBlock=atoi(papszArgv[++iArg]);
			//printf("nblock is %d\n",nBlock);
		}
        else if( EQUAL(papszArgv[iArg],"-fid") && papszArgv[iArg+1] != NULL )
        {
            nFIDToFetch = atoi(papszArgv[++iArg]);
        }
        else if( EQUAL(papszArgv[iArg],"-sql") && papszArgv[iArg+1] != NULL )
        {
            pszSQLStatement = papszArgv[++iArg];
        }
        else if( EQUAL(papszArgv[iArg],"-dialect") && papszArgv[iArg+1] != NULL )
        {
            pszDialect = papszArgv[++iArg];
        }
        else if( EQUAL(papszArgv[iArg],"-nln") && iArg < nArgc-1 )
        {
            pszNewLayerName = CPLStrdup(papszArgv[++iArg]);
        }
        else if( EQUAL(papszArgv[iArg],"-nlt") && iArg < nArgc-1 )
        {
            int bIs3D = FALSE;
            CPLString osGeomName = papszArgv[iArg+1];
            if (strlen(papszArgv[iArg+1]) > 3 &&
                EQUALN(papszArgv[iArg+1] + strlen(papszArgv[iArg+1]) - 3, "25D", 3))
            {
                bIs3D = TRUE;
                osGeomName.resize(osGeomName.size() - 3);
            }
            if( EQUAL(osGeomName,"NONE") )
                eGType = wkbNone;
            else if( EQUAL(osGeomName,"GEOMETRY") )
                eGType = wkbUnknown;
            else
            {
                eGType = OGRFromOGCGeomType(osGeomName);
                if (eGType == wkbUnknown)
                {
                    fprintf( stderr, "-nlt %s: type not recognised.\n",
                            papszArgv[iArg+1] );
                    exitMPI( 1 );
                }
            }
            if (eGType != wkbNone && bIs3D)
                eGType |= wkb25DBit;

            iArg++;
        }
        //else if( (EQUAL(papszArgv[iArg],"-tg") ||
        //          EQUAL(papszArgv[iArg],"-gt")) && iArg < nArgc-1 )
        //{
        //    nGroupTransactions = atoi(papszArgv[++iArg]);
        //}
        //else if( EQUAL(papszArgv[iArg],"-s_srs") && iArg < nArgc-1 )
        //{
        //    pszSourceSRSDef = papszArgv[++iArg];
        //}
        //else if( EQUAL(papszArgv[iArg],"-a_srs") && iArg < nArgc-1 )
        //{
        //    pszOutputSRSDef = papszArgv[++iArg];
        //    if (EQUAL(pszOutputSRSDef, "NULL") ||
        //        EQUAL(pszOutputSRSDef, "NONE"))
        //    {
        //        pszOutputSRSDef = NULL;
        //        bNullifyOutputSRS = TRUE;
        //    }
        //}
        //else if( EQUAL(papszArgv[iArg],"-t_srs") && iArg < nArgc-1 )
        //{
        //    pszOutputSRSDef = papszArgv[++iArg];
        //    bTransform = TRUE;
        //}
        else if( EQUAL(papszArgv[iArg],"-spat") 
                 && papszArgv[iArg+1] != NULL 
                 && papszArgv[iArg+2] != NULL 
                 && papszArgv[iArg+3] != NULL 
                 && papszArgv[iArg+4] != NULL )
        {
            OGRLinearRing  oRing;

            oRing.addPoint( atof(papszArgv[iArg+1]), atof(papszArgv[iArg+2]) );
            oRing.addPoint( atof(papszArgv[iArg+1]), atof(papszArgv[iArg+4]) );
            oRing.addPoint( atof(papszArgv[iArg+3]), atof(papszArgv[iArg+4]) );
            oRing.addPoint( atof(papszArgv[iArg+3]), atof(papszArgv[iArg+2]) );
            oRing.addPoint( atof(papszArgv[iArg+1]), atof(papszArgv[iArg+2]) );

            poSpatialFilter = OGRGeometryFactory::createGeometry(wkbPolygon);
            ((OGRPolygon *) poSpatialFilter)->addRing( &oRing );
            iArg += 4;
        }
        else if( EQUAL(papszArgv[iArg],"-where") && papszArgv[iArg+1] != NULL )
        {
            pszWHERE = papszArgv[++iArg];
        }
        else if( EQUAL(papszArgv[iArg],"-select") && papszArgv[iArg+1] != NULL)
        {
            pszSelect = papszArgv[++iArg];
            papszSelFields = CSLTokenizeStringComplex(pszSelect, " ,", 
                                                      FALSE, FALSE );
        }
        //else if( EQUAL(papszArgv[iArg],"-segmentize") && iArg < nArgc-1 )
        //{
        //    dfMaxSegmentLength = atof(papszArgv[++iArg]);
        //}
        else if( EQUAL(papszArgv[iArg],"-fieldTypeToString") && iArg < nArgc-1 )
        {
            papszFieldTypesToString =
                    CSLTokenizeStringComplex(papszArgv[++iArg], " ,", 
                                             FALSE, FALSE );
            char** iter = papszFieldTypesToString;
            while(*iter)
            {
                if (EQUAL(*iter, "Integer") ||
                    EQUAL(*iter, "Real") ||
                    EQUAL(*iter, "String") ||
                    EQUAL(*iter, "Date") ||
                    EQUAL(*iter, "Time") ||
                    EQUAL(*iter, "DateTime") ||
                    EQUAL(*iter, "Binary") ||
                    EQUAL(*iter, "IntegerList") ||
                    EQUAL(*iter, "RealList") ||
                    EQUAL(*iter, "StringList"))
                {
                    /* Do nothing */
                }
                else if (EQUAL(*iter, "All"))
                {
                    CSLDestroy(papszFieldTypesToString);
                    papszFieldTypesToString = NULL;
                    papszFieldTypesToString = CSLAddString(papszFieldTypesToString, "All");
                    break;
                }
                else
                {
                    fprintf(stderr, "Unhandled type for fieldtypeasstring option : %s\n",
                            *iter);
                    Usage();
                }
                iter ++;
            }
        }
        //else if( EQUAL(papszArgv[iArg],"-progress") )
        //{
        //    bDisplayProgress = TRUE;
        //}
        //else if( EQUAL(papszArgv[iArg],"-wrapdateline") )
        //{
        //    bWrapDateline = TRUE;
        //}
        else if( EQUAL(papszArgv[iArg],"-clipsrc") && iArg < nArgc-1 )
        {
            bClipSrc = TRUE;
            if ( IsNumber(papszArgv[iArg+1])
                 && papszArgv[iArg+2] != NULL 
                 && papszArgv[iArg+3] != NULL 
                 && papszArgv[iArg+4] != NULL)
            {
                OGRLinearRing  oRing;

                oRing.addPoint( atof(papszArgv[iArg+1]), atof(papszArgv[iArg+2]) );
                oRing.addPoint( atof(papszArgv[iArg+1]), atof(papszArgv[iArg+4]) );
                oRing.addPoint( atof(papszArgv[iArg+3]), atof(papszArgv[iArg+4]) );
                oRing.addPoint( atof(papszArgv[iArg+3]), atof(papszArgv[iArg+2]) );
                oRing.addPoint( atof(papszArgv[iArg+1]), atof(papszArgv[iArg+2]) );

                poClipSrc = OGRGeometryFactory::createGeometry(wkbPolygon);
                ((OGRPolygon *) poClipSrc)->addRing( &oRing );
                iArg += 4;
            }
            else if (EQUALN(papszArgv[iArg+1], "POLYGON", 7) ||
                     EQUALN(papszArgv[iArg+1], "MULTIPOLYGON", 12))
            {
                char* pszTmp = (char*) papszArgv[iArg+1];
                OGRGeometryFactory::createFromWkt(&pszTmp, NULL, &poClipSrc);
                if (poClipSrc == NULL)
                {
                    fprintf( stderr, "FAILURE: Invalid geometry. Must be a valid POLYGON or MULTIPOLYGON WKT\n\n");
                    Usage();
                }
                iArg ++;
            }
            else if (EQUAL(papszArgv[iArg+1], "spat_extent") )
            {
                iArg ++;
            }
            else
            {
                pszClipSrcDS = papszArgv[iArg+1];
                iArg ++;
            }
        }
        else if( EQUAL(papszArgv[iArg],"-clipsrcsql") && iArg < nArgc-1 )
        {
            pszClipSrcSQL = papszArgv[iArg+1];
            iArg ++;
        }
        else if( EQUAL(papszArgv[iArg],"-clipsrclayer") && iArg < nArgc-1 )
        {
            pszClipSrcLayer = papszArgv[iArg+1];
            iArg ++;
        }
        else if( EQUAL(papszArgv[iArg],"-clipsrcwhere") && iArg < nArgc-1 )
        {
            pszClipSrcWhere = papszArgv[iArg+1];
            iArg ++;
        }
        /*else if( EQUAL(papszArgv[iArg],"-clipdst") && iArg < nArgc-1 )
        {
            if ( IsNumber(papszArgv[iArg+1])
                 && papszArgv[iArg+2] != NULL 
                 && papszArgv[iArg+3] != NULL 
                 && papszArgv[iArg+4] != NULL)
            {
                OGRLinearRing  oRing;

                oRing.addPoint( atof(papszArgv[iArg+1]), atof(papszArgv[iArg+2]) );
                oRing.addPoint( atof(papszArgv[iArg+1]), atof(papszArgv[iArg+4]) );
                oRing.addPoint( atof(papszArgv[iArg+3]), atof(papszArgv[iArg+4]) );
                oRing.addPoint( atof(papszArgv[iArg+3]), atof(papszArgv[iArg+2]) );
                oRing.addPoint( atof(papszArgv[iArg+1]), atof(papszArgv[iArg+2]) );

                poClipDst = OGRGeometryFactory::createGeometry(wkbPolygon);
                ((OGRPolygon *) poClipDst)->addRing( &oRing );
                iArg += 4;
            }
            else if (EQUALN(papszArgv[iArg+1], "POLYGON", 7) ||
                     EQUALN(papszArgv[iArg+1], "MULTIPOLYGON", 12))
            {
                char* pszTmp = (char*) papszArgv[iArg+1];
                OGRGeometryFactory::createFromWkt(&pszTmp, NULL, &poClipDst);
                if (poClipDst == NULL)
                {
                    fprintf( stderr, "FAILURE: Invalid geometry. Must be a valid POLYGON or MULTIPOLYGON WKT\n\n");
                    Usage();
                }
                iArg ++;
            }
            else
            {
                pszClipDstDS = papszArgv[iArg+1];
                iArg ++;
            }
        }
        else if( EQUAL(papszArgv[iArg],"-clipdstsql") && iArg < nArgc-1 )
        {
            pszClipDstSQL = papszArgv[iArg+1];
            iArg ++;
        }
        else if( EQUAL(papszArgv[iArg],"-clipdstlayer") && iArg < nArgc-1 )
        {
            pszClipDstLayer = papszArgv[iArg+1];
            iArg ++;
        }
        else if( EQUAL(papszArgv[iArg],"-clipdstwhere") && iArg < nArgc-1 )
        {
            pszClipDstWhere = papszArgv[iArg+1];
            iArg ++;
        }*/
        //else if( EQUAL(papszArgv[iArg],"-splitlistfields") )
        //{
        //    bSplitListFields = TRUE;
        //}
        //else if ( EQUAL(papszArgv[iArg],"-maxsubfields") && iArg < nArgc-1 )
        //{
        //    if (IsNumber(papszArgv[iArg+1]))
        //    {
        //        int nTemp = atoi(papszArgv[iArg+1]);
        //        if (nTemp > 0)
        //        {
        //            nMaxSplitListSubFields = nTemp;
        //            iArg ++;
        //        }
        //    }
        //}
        //else if( EQUAL(papszArgv[iArg],"-explodecollections") )
        //{
        //    bExplodeCollections = TRUE;
        //}
        //else if( EQUAL(papszArgv[iArg],"-zfield") && iArg < nArgc-1 )
        //{
        //    pszZField = papszArgv[iArg+1];
        //    iArg ++;
        //}
        else if( papszArgv[iArg][0] == '-' )
        {
            Usage();
        }
        else if( pszDestDataSource == NULL )
            pszDestDataSource = papszArgv[iArg];
        else if( pszDataSource == NULL )
            pszDataSource = papszArgv[iArg];
        else
            papszLayers = CSLAddString( papszLayers, papszArgv[iArg] );
    }

    if( pszDataSource == NULL )
        Usage();

    //if( bPreserveFID && bExplodeCollections )
    //{
    //    fprintf( stderr, "FAILURE: cannot use -preserve_fid and -explodecollections at the same time\n\n" );
    //    Usage();
    //}

    if( bClipSrc && pszClipSrcDS != NULL)
    {
        poClipSrc = LoadGeometry(pszClipSrcDS, pszClipSrcSQL, pszClipSrcLayer, pszClipSrcWhere);
        if (poClipSrc == NULL)
        {
            fprintf( stderr, "FAILURE: cannot load source clip geometry\n\n" );
            Usage();
        }
    }
    else if( bClipSrc && poClipSrc == NULL )
    {
        if (poSpatialFilter)
            poClipSrc = poSpatialFilter->clone();
        if (poClipSrc == NULL)
        {
            fprintf( stderr, "FAILURE: -clipsrc must be used with -spat option or a\n"
                             "bounding box, WKT string or datasource must be specified\n\n");
            Usage();
        }
    }
    
    //if( pszClipDstDS != NULL)
    //{
    //    poClipDst = LoadGeometry(pszClipDstDS, pszClipDstSQL, pszClipDstLayer, pszClipDstWhere);
    //    if (poClipDst == NULL)
    //    {
    //        fprintf( stderr, "FAILURE: cannot load dest clip geometry\n\n" );
    //        Usage();
    //    }
    //}
    
/* -------------------------------------------------------------------- */
/*   Open data source.                                            */
/* -------------------------------------------------------------------- */
    const char  *pszSFormat = NULL;
	OGRDataSource       *poDS;
	OGRSFDriver         *poSDriver = NULL;
	
    poDS = OGRSFDriverRegistrar::Open( pszDataSource, FALSE,&poSDriver);
	if(poSDriver!=NULL)
	{
		pszSFormat=poSDriver->GetName();
		//printf("source layer format is %s\n",pszSFormat);
	}

/* -------------------------------------------------------------------- */
/*   Report failure                                                  */
/* -------------------------------------------------------------------- */
    if( poDS == NULL )
    {
        OGRSFDriverRegistrar    *poR = OGRSFDriverRegistrar::GetRegistrar();
        
        fprintf( stderr, "FAILURE:\n"
                "Unable to open datasource `%s' with the following drivers.\n",
                pszDataSource );

        for( int iDriver = 0; iDriver < poR->GetDriverCount(); iDriver++ )
        {
            fprintf( stderr, "  -> %s\n", poR->GetDriver(iDriver)->GetName() );
        }

       exitMPI(1);
    }

/* -------------------------------------------------------------------- */
/*    Try opening the output datasource as an existing, writable      */
/* -------------------------------------------------------------------- */
    OGRDataSource       *poODS = NULL;
   
	if (cp==master)
	{ 
		OGRSFDriver          *poDriver = NULL;
		if( bUpdate )
		{
			poODS = OGRSFDriverRegistrar::Open( pszDestDataSource, TRUE, &poDriver );
			if( poODS == NULL )
			{
				if (bOverwrite || bAppend)
				{
					poODS = OGRSFDriverRegistrar::Open( pszDestDataSource, FALSE, &poDriver );
					if (poODS == NULL)
					{
						/* ok the datasource doesn't exist at all */
						bUpdate = FALSE;
					}
					else
					{
						OGRDataSource::DestroyDataSource(poODS);
						poODS = NULL;
					}
				}

				if (bUpdate)
				{
					fprintf( stderr, "FAILURE:\n"
							"Unable to open existing output datasource `%s'.\n",
							pszDestDataSource );
					exitMPI(1);
				}
			}
			else if( CSLCount(papszDSCO) > 0 )
			{
				fprintf( stderr, "WARNING: Datasource creation options ignored since an existing datasource\n"
						"         being updated.\n" );
			}
		}

	/* -------------------------------------------------------------------- */
	/*      Find the output driver.   使用主进行目标数据源的创建                                    */
	/* -------------------------------------------------------------------- */
		if( !bUpdate )
		{
			OGRSFDriverRegistrar *poR = OGRSFDriverRegistrar::GetRegistrar();
			int                  iDriver;

			for( iDriver = 0;
				 iDriver < poR->GetDriverCount() && poDriver == NULL;
				 iDriver++ )
			{
				if( EQUAL(poR->GetDriver(iDriver)->GetName(),pszFormat) )
				{
					poDriver = poR->GetDriver(iDriver);
				}
			}

			if( poDriver == NULL )
			{
				fprintf( stderr, "Unable to find driver '%s'.\n", pszFormat );
				fprintf( stderr,  "The following drivers are available:\n" );
	        
				for( iDriver = 0; iDriver < poR->GetDriverCount(); iDriver++ )
				{
					fprintf( stderr,  "  -> `%s'\n", poR->GetDriver(iDriver)->GetName() );
				}
				exitMPI(1);
			}

			if( !poDriver->TestCapability( ODrCCreateDataSource ) )
			{
				fprintf( stderr,  "%s driver does not support data source creation.\n",
						pszFormat );
				exitMPI(1);
			}

	/* -------------------------------------------------------------------- */
	/*      Create the output data source.                                  */
	/* -------------------------------------------------------------------- */
			poODS = poDriver->CreateDataSource( pszDestDataSource, papszDSCO );
			if( poODS == NULL )
			{
				fprintf( stderr,  "%s driver failed to create %s\n", 
						pszFormat, pszDestDataSource );
				exitMPI(1);
			}
			printf("\n process %d have created the DestDataSource : %s\n",cp,pszDestDataSource);
		}
	}


/* -------------------------------------------------------------------- */
/*      Parse the output SRS definition if possible. （多个进程，解析输出投影坐标）                   */
/* -------------------------------------------------------------------- */
    //if( pszOutputSRSDef != NULL )
    //{
    //    poOutputSRS = (OGRSpatialReference*)OSRNewSpatialReference(NULL);
    //    if( poOutputSRS->SetFromUserInput( pszOutputSRSDef ) != OGRERR_NONE )
    //    {
    //        fprintf( stderr,  "Failed to process SRS definition: %s\n", 
    //                pszOutputSRSDef );
    //        exitMPI(1);
    //    }
    //}

//* -------------------------------------------------------------------- */
//*      Parse the source SRS definition if possible.  （多个进程，解析源投影坐标）                  */
//* -------------------------------------------------------------------- */
    //if( pszSourceSRSDef != NULL )
    //{
    //    poSourceSRS = (OGRSpatialReference*)OSRNewSpatialReference(NULL);
    //    if( poSourceSRS->SetFromUserInput( pszSourceSRSDef ) != OGRERR_NONE )
    //    {
    //        fprintf( stderr,  "Failed to process SRS definition: %s\n", 
    //                pszSourceSRSDef );
    //       exitMPI(1);;
    //    }
    //}



///* -------------------------------------------------------------------- */
///*      Special case for -sql clause.  No source layers required.  */
///* -------------------------------------------------------------------- */
    if( pszSQLStatement != NULL )
    {
        OGRLayer *poResultSet;

        if( pszWHERE != NULL )
            fprintf( stderr,  "-where clause ignored in combination with -sql.\n" );
        if( CSLCount(papszLayers) > 0 )
            fprintf( stderr,  "layer names ignored in combination with -sql.\n" );
        
        poResultSet = poDS->ExecuteSQL( pszSQLStatement, poSpatialFilter, 
                                        pszDialect );

        if( poResultSet != NULL )
		{
            //long nCountLayerFeatures = 0;
            //if (bDisplayProgress)
            //{
            //    if (!poResultSet->TestCapability(OLCFastFeatureCount))
            //    {
            //        fprintf( stderr, "Progress turned off as fast feature count is not available.\n");
            //        bDisplayProgress = FALSE;
            //    }
            //    else
            //    {
            //        nCountLayerFeatures = poResultSet->GetFeatureCount();//获取feature数量
            //        pfnProgress = GDALTermProgress;
            //    }
            //}

   //         OGRLayer* poPassedLayer = poResultSet;
   //         if (bSplitListFields)
   //         {
   //             poPassedLayer = new OGRSplitListFieldLayer(poPassedLayer, nMaxSplitListSubFields);
   //             int nRet = ((OGRSplitListFieldLayer*)poPassedLayer)->BuildLayerDefn(NULL, NULL);
   //             if (!nRet)
   //             {
   //                 delete poPassedLayer;
   //                 poPassedLayer = poResultSet;
   //             }
			//}
		
		
//* -------------------------------------------------------------------- */
//*      Special case to improve user experience when translating into   */
//*      single file shapefile and source has only one layer, and that   */
//*      the layer name isn't specified  并行环境下0进程以外的其他进程poDriver为空  */
//* -------------------------------------------------------------------- */
            //VSIStatBufL  sStat;
            //if (EQUAL(poDriver->GetName(), "ESRI Shapefile") &&
            //    pszNewLayerName == NULL &&
            //    VSIStatL(pszDestDataSource, &sStat) == 0 && VSI_ISREG(sStat.st_mode))
            //{
            //    pszNewLayerName = CPLStrdup(CPLGetBasename(pszDestDataSource));
            //}	
		//实行转换函数
			//printf(" the DestDataSource : %s\n",pszDestDataSource);
            if( !TranslateLayer( poDS, /*poPassedLayer*/poResultSet, poODS, papszLCO, 
                                 pszNewLayerName, /*bTransform, poOutputSRS, bNullifyOutputSRS,
                                 poSourceSRS,*/ papszSelFields, bAppend, eGType,
                                 bOverwrite, /*dfMaxSegmentLength,*/ papszFieldTypesToString,
                                /* nCountLayerFeatures, *//*bWrapDateline,*/ poClipSrc, /*poClipDst,*/
                                /* bExplodeCollections, pszZField, */pszWHERE, /*pfnProgress, pProgressArg,*/pszDestDataSource,pszSFormat))
            {
				
                CPLError( CE_Failure, CPLE_AppDefined, 
                          "Terminating translation prematurely after failed\n"
                          "translation from sql statement." );

               exitMPI(1);
            }

            //if (poPassedLayer != poResultSet)
            //    delete poPassedLayer;

            poDS->ReleaseResultSet( poResultSet );
        }
    }

    else
    {
        int nLayerCount = 0;
        OGRLayer** papoLayers = NULL;

//* -------------------------------------------------------------------- */
//*      Process each data source layer.                                 */
//* -------------------------------------------------------------------- */
        if ( CSLCount(papszLayers) == 0)
        {
            nLayerCount = poDS->GetLayerCount();
            papoLayers = (OGRLayer**)CPLMalloc(sizeof(OGRLayer*) * nLayerCount);

            for( int iLayer = 0; iLayer < nLayerCount; iLayer++ )
            {
				
                OGRLayer        *poLayer = poDS->GetLayer(iLayer);

				printf("%s",poLayer->GetName());
                if( poLayer == NULL )
                {
                    fprintf( stderr, "FAILURE: Couldn't fetch advertised layer %d!\n",
                            iLayer );
                    exitMPI(1);
                }

                papoLayers[iLayer] = poLayer;
            }
			
        }
//* -------------------------------------------------------------------- */
//*      Process specified data source layers.                           */
//* -------------------------------------------------------------------- */
        else
        {
            nLayerCount = CSLCount(papszLayers);
            papoLayers = (OGRLayer**)CPLMalloc(sizeof(OGRLayer*) * nLayerCount);

            for( int iLayer = 0; 
                papszLayers[iLayer] != NULL; 
                iLayer++ )
            {
                OGRLayer        *poLayer = poDS->GetLayerByName(papszLayers[iLayer]);

                if( poLayer == NULL )
                {
                    fprintf( stderr, "FAILURE: Couldn't fetch requested layer '%s'!\n",
                             papszLayers[iLayer] );
                    if (!bSkipFailures)
                        exitMPI(1);
                }

                papoLayers[iLayer] = poLayer;
            }
        }

//* -------------------------------------------------------------------- */
//*      Special case to improve user experience when translating into   */
//*      single file shapefile and source has only one layer, and that   */
//*      the layer name isn't specified 并行环境下0进程以外的其他进程poDriver为空  */
//* -------------------------------------------------------------------- */
		
		//VSIStatBufL  sStat;
		//if(EQUAL(poDriver->GetName(), "ESRI Shapefile") &&
		//        nLayerCount == 1 && pszNewLayerName == NULL &&
		//        VSIStatL(pszDestDataSource, &sStat) == 0 && VSI_ISREG(sStat.st_mode))
		// {
		//        pszNewLayerName = CPLStrdup(CPLGetBasename(pszDestDataSource));
		// }
		
        //long* panLayerCountFeatures = (long*) CPLMalloc(sizeof(long) * nLayerCount);//各图层要素个数数组
		//for( int i=0; i<nLayerCount; i++)
		//	panLayerCountFeatures [i] = -1;
        //long nCountLayersFeatures = 0;//所有图层要素个数
        //long nAccCountFeatures = 0;//已经处理的要素个数
        int iLayer;

        /* First pass to apply filters and count all features if necessary */
        for( iLayer = 0; iLayer < nLayerCount; iLayer++ )
        {
            OGRLayer        *poLayer = papoLayers[iLayer];
            if (poLayer == NULL)
                continue;

            if( pszWHERE != NULL )
                poLayer->SetAttributeFilter( pszWHERE );

            if( poSpatialFilter != NULL )
                poLayer->SetSpatialFilter( poSpatialFilter );

            //if (bDisplayProgress)
            //{
            //    if (!poLayer->TestCapability(OLCFastFeatureCount))
            //    {
            //        fprintf( stderr, "Progress turned off as fast feature count is not available.\n");
            //        bDisplayProgress = FALSE;
            //    }
            //    else
            //    {
            //        panLayerCountFeatures[iLayer] = poLayer->GetFeatureCount();//制定进度条显示参数才赋值
            //        nCountLayersFeatures += panLayerCountFeatures[iLayer];//获取所有图层要素个数
					
            //    }
            //}
        }

        /* Second pass to do the real job（处理多个图层数据） */
        for( iLayer = 0; 
            iLayer < nLayerCount; 
            iLayer++ )
        {
            OGRLayer        *poLayer = papoLayers[iLayer];
            if (poLayer == NULL)
                continue;


            //OGRLayer* poPassedLayer = poLayer;
            //if (bSplitListFields)
            //{
            //    poPassedLayer = new OGRSplitListFieldLayer(poPassedLayer, nMaxSplitListSubFields);

            //    if (bDisplayProgress && nMaxSplitListSubFields != 1)
            //    {
            //        pfnProgress = GDALScaledProgress;
            //        pProgressArg = 
            //            GDALCreateScaledProgress(nAccCountFeatures * 1.0 / nCountLayersFeatures,
            //                                    (nAccCountFeatures + panLayerCountFeatures[iLayer] / 2) * 1.0 / nCountLayersFeatures,
            //                                    GDALTermProgress,
            //                                    NULL);
            //    }
            //    else
            //    {
            //        pfnProgress = NULL;
            //        pProgressArg = NULL;
            //    }

            //    int nRet = ((OGRSplitListFieldLayer*)poPassedLayer)->BuildLayerDefn(pfnProgress, pProgressArg);
            //    if (!nRet)
            //    {
            //        delete poPassedLayer;
            //        poPassedLayer = poLayer;
            //    }

            //    if (bDisplayProgress)
            //        GDALDestroyScaledProgress(pProgressArg);
            //}


            //if (bDisplayProgress)
            //{
            //    pfnProgress = GDALScaledProgress;
            //    int nStart = 0;
            //    if (poPassedLayer != poLayer && nMaxSplitListSubFields != 1)
            //        nStart = panLayerCountFeatures[iLayer] / 2;
            //    pProgressArg = 
            //        GDALCreateScaledProgress((nAccCountFeatures + nStart) * 1.0 / nCountLayersFeatures,
            //                                (nAccCountFeatures + panLayerCountFeatures[iLayer]) * 1.0 / nCountLayersFeatures,
            //                                GDALTermProgress,
            //                                NULL);
            //}

            //nAccCountFeatures += panLayerCountFeatures[iLayer];	//已经处理的要素个数
				
			//多层的数据逐层进行数据处理转换
			//printf("featurecount is %d\n",panLayerCountFeatures[iLayer]);
            if( !TranslateLayer( poDS, /*poPassedLayer*/poLayer, poODS, papszLCO, 
                                pszNewLayerName, /*bTransform, poOutputSRS, bNullifyOutputSRS,
                                poSourceSRS,*/ papszSelFields, bAppend, eGType,
                                bOverwrite,/* dfMaxSegmentLength,*/ papszFieldTypesToString,
                                /*panLayerCountFeatures[iLayer],*/ /*bWrapDateline,*/ poClipSrc, /*poClipDst,*/
                                /*bExplodeCollections, pszZField,*/ pszWHERE,/* pfnProgress, pProgressArg,*/pszDestDataSource,pszSFormat)
                && !bSkipFailures )

            {
                
				CPLError( CE_Failure, CPLE_AppDefined, 
                        "Terminating translation prematurely after failed\n"
                        "translation of layer %s (use -skipfailures to skip errors)\n", 
                        poLayer->GetName() );

                exitMPI(1);
            }
			

            //if (poPassedLayer != poLayer)
            //    delete poPassedLayer;

            //if (bDisplayProgress)
            //    GDALDestroyScaledProgress(pProgressArg);
        }

        //CPLFree(panLayerCountFeatures);
        CPLFree(papoLayers);
    }

	/*the end time*/
	time2 = MPI_Wtime();
	printf("This is process No.< %d >, and I've done my job in < %f > seconds!\n",cp,time2-time1);
	if(cp==0){
	 	ofstream logs;
		char *filename="/share/rdata/prj2/v2v_test.log";
		
		logs.open(filename,ios::app);
		logs<<np<<"\t"<<time2-time1<<endl;
		logs.close();
	}



/* -------------------------------------------------------------------- */
/*      Process DS style table                                          */
/* -------------------------------------------------------------------- */

    //poODS->SetStyleTable( poDS->GetStyleTable () );
    
/* -------------------------------------------------------------------- */
/*      Close down.                                                     */
/* -------------------------------------------------------------------- */
    //OGRSpatialReference::DestroySpatialReference(poOutputSRS);
    //OGRSpatialReference::DestroySpatialReference(poSourceSRS);
    //OGRDataSource::DestroyDataSource(poODS);
    OGRDataSource::DestroyDataSource(poDS);
    OGRGeometryFactory::destroyGeometry(poSpatialFilter);
    OGRGeometryFactory::destroyGeometry(poClipSrc);
    //OGRGeometryFactory::destroyGeometry(poClipDst);

    CSLDestroy(papszSelFields);
    CSLDestroy( papszArgv );
    CSLDestroy( papszLayers );
    CSLDestroy( papszDSCO );
    CSLDestroy( papszLCO );
    CSLDestroy( papszFieldTypesToString );
    CPLFree( pszNewLayerName );

    OGRCleanupAll();

#ifdef DBMALLOC
    malloc_dump(1);
#endif

  
    
	MPI_Finalize() ;
	return 0;
}




/************************************************************************/
/*                               Usage()                                */
/************************************************************************/

static void Usage(int bShort)

{
    OGRSFDriverRegistrar        *poR = OGRSFDriverRegistrar::GetRegistrar();


    printf( "Usage: ogr2ogr [--help-general] [-skipfailures] [-append] [-update]\n"
            "               [-select field_list] [-where restricted_where]\n"
            "               [-progress] [-sql <sql statement>] [-dialect dialect]\n"
            "               [-preserve_fid] [-fid FID]\n"
            "               [-spat xmin ymin xmax ymax]\n"
            "               [-a_srs srs_def] [-t_srs srs_def] [-s_srs srs_def]\n"
            "               [-f format_name] [-overwrite] [[-dsco NAME=VALUE] ...]\n"
            "               dst_datasource_name src_datasource_name\n"
            "               [-lco NAME=VALUE] [-nln name] [-nlt type] [layer [layer ...]]\n"
            "\n"
            "Advanced options :\n"
            "               [-gt n]\n"
            "               [-clipsrc [xmin ymin xmax ymax]|WKT|datasource|spat_extent]\n"
            "               [-clipsrcsql sql_statement] [-clipsrclayer layer]\n"
            "               [-clipsrcwhere expression]\n"
            "               [-clipdst [xmin ymin xmax ymax]|WKT|datasource]\n"
            "               [-clipdstsql sql_statement] [-clipdstlayer layer]\n"
            "               [-clipdstwhere expression]\n"
            "               [-wrapdateline]\n"
            "               [-segmentize max_dist] [-fieldTypeToString All|(type1[,type2]*)]\n"
            "               [-splitlistfields] [-maxsubfields val]\n"
            "               [-explodecollections] [-zfield field_name]\n");

    if (bShort)
    {
        printf( "\nNote: ogr2ogr --long-usage for full help.\n");
        exitMPI( 1 );
    }

    printf("\n -f format_name: output file format name, possible values are:\n");

    for( int iDriver = 0; iDriver < poR->GetDriverCount(); iDriver++ )
    {
        OGRSFDriver *poDriver = poR->GetDriver(iDriver);

        if( poDriver->TestCapability( ODrCCreateDataSource ) )
            printf( "     -f \"%s\"\n", poDriver->GetName() );
    }

    printf( " -append: Append to existing layer instead of creating new if it exists\n"
            " -overwrite: delete the output layer and recreate it empty\n"
            " -update: Open existing output datasource in update mode\n"
            " -progress: Display progress on terminal. Only works if input layers have the \n"
            "                                          \"fast feature count\" capability\n"
            " -select field_list: Comma-delimited list of fields from input layer to\n"
            "                     copy to the new layer (defaults to all)\n" 
            " -where restricted_where: Attribute query (like SQL WHERE)\n" 
            " -wrapdateline: split geometries crossing the dateline meridian\n"
            "                (long. = +/- 180deg)\n" 
            " -sql statement: Execute given SQL statement and save result.\n"
            " -dialect value: select a dialect, usually OGRSQL to avoid native sql.\n"
            " -skipfailures: skip features or layers that fail to convert\n"
            " -gt n: group n features per transaction (default 200)\n"
            " -spat xmin ymin xmax ymax: spatial query extents\n"
            " -segmentize max_dist: maximum distance between 2 nodes.\n"
            "                       Used to create intermediate points\n"
            " -dsco NAME=VALUE: Dataset creation option (format specific)\n"
            " -lco  NAME=VALUE: Layer creation option (format specific)\n"
            " -nln name: Assign an alternate name to the new layer\n"
            " -nlt type: Force a geometry type for new layer.  One of NONE, GEOMETRY,\n"
            "      POINT, LINESTRING, POLYGON, GEOMETRYCOLLECTION, MULTIPOINT,\n"
            "      MULTIPOLYGON, or MULTILINESTRING.  Add \"25D\" for 3D layers.\n"
            "      Default is type of source layer.\n"
            " -fieldTypeToString type1,...: Converts fields of specified types to\n"
            "      fields of type string in the new layer. Valid types are : Integer,\n"
            "      Real, String, Date, Time, DateTime, Binary, IntegerList, RealList,\n"
            "      StringList. Special value All will convert all fields to strings.\n");

    printf(" -a_srs srs_def: Assign an output SRS\n"
           " -t_srs srs_def: Reproject/transform to this SRS on output\n"
           " -s_srs srs_def: Override source SRS\n"
           "\n" 
           " Srs_def can be a full WKT definition (hard to escape properly),\n"
           " or a well known definition (ie. EPSG:4326) or a file with a WKT\n"
           " definition.\n" );

   exitMPI(1);
}

/************************************************************************/
/*                               SetZ()                                 */
/************************************************************************/
static void SetZ (OGRGeometry* poGeom, double dfZ )
{
    if (poGeom == NULL)
        return;
    switch (wkbFlatten(poGeom->getGeometryType()))
    {
        case wkbPoint:
            ((OGRPoint*)poGeom)->setZ(dfZ);
            break;

        case wkbLineString:
        case wkbLinearRing:
        {
            int i;
            OGRLineString* poLS = (OGRLineString*) poGeom;
            for(i=0;i<poLS->getNumPoints();i++)
                poLS->setPoint(i, poLS->getX(i), poLS->getY(i), dfZ);
            break;
        }

        case wkbPolygon:
        {
            int i;
            OGRPolygon* poPoly = (OGRPolygon*) poGeom;
            SetZ(poPoly->getExteriorRing(), dfZ);
            for(i=0;i<poPoly->getNumInteriorRings();i++)
                SetZ(poPoly->getInteriorRing(i), dfZ);
            break;
        }

        case wkbMultiPoint:
        case wkbMultiLineString:
        case wkbMultiPolygon:
        case wkbGeometryCollection:
        {
            int i;
            OGRGeometryCollection* poGeomColl = (OGRGeometryCollection*) poGeom;
            for(i=0;i<poGeomColl->getNumGeometries();i++)
                SetZ(poGeomColl->getGeometryRef(i), dfZ);
            break;
        }

        default:
            break;
    }
}


/************************************************************************/
/*                           TranslateLayer()                           */
/************************************************************************/

static int TranslateLayer( OGRDataSource *poSrcDS, 
                           OGRLayer * poSrcLayer,
                           OGRDataSource *poDstDS,
                           char **papszLCO,
                           const char *pszNewLayerName,
                           //int bTransform, 
                           //OGRSpatialReference *poOutputSRS,
                           //int bNullifyOutputSRS,
                           //OGRSpatialReference *poSourceSRS,
                           char **papszSelFields,
                           int bAppend, int eGType, int bOverwrite,
                           //double dfMaxSegmentLength,
                           char** papszFieldTypesToString,
                           //long nCountLayerFeatures,
                           //int bWrapDateline,
                           OGRGeometry* poClipSrc,
                           //OGRGeometry *poClipDst,
                           //int bExplodeCollections,
                           //const char* pszZField,
                           const char* pszWHERE,
                           //GDALProgressFunc pfnProgress,
                           //void *pProgressArg,
						   const char *pszDestDataSource,
						   const char *pszSFormat						  
						   )

{
   
	//获取进程编号 
	int cp, np;
	MPI_Status status;

	MPI_Comm_rank(MPI_COMM_WORLD, &cp);
	MPI_Comm_size(MPI_COMM_WORLD, &np);
	int msg=0;
	//double time3,time4,time5,time6;


//用多个进程进行测试？
	
	OGRLayer    *poDstLayer=NULL;
    OGRFeatureDefn *poSrcFDefn;
   

    char**      papszTransformOptions = NULL;

    if( pszNewLayerName == NULL )
        pszNewLayerName = poSrcLayer->GetName();

	
/* -------------------------------------------------------------------- */
/*      Setup coordinate transformation if we need it. （以图层为单位，获取源投影和目标投影，并建立其对应关系）                 */
/* -------------------------------------------------------------------- */
    //OGRCoordinateTransformation *poCT = NULL;

    //if( bTransform )
    //{
    //    if( poSourceSRS == NULL )
    //        poSourceSRS = poSrcLayer->GetSpatialRef();

    //    if( poSourceSRS == NULL )
    //    {
    //        fprintf( stderr, "Can't transform coordinates, source layer has no\n"
    //                "coordinate system.  Use -s_srs to set one.\n" );
    //       exitMPI(1);
    //    }

    //    CPLAssert( NULL != poSourceSRS );
    //    CPLAssert( NULL != poOutputSRS );

    //    poCT = OGRCreateCoordinateTransformation( poSourceSRS, poOutputSRS );
    //    if( poCT == NULL )
    //    {
    //        char        *pszWKT = NULL;

    //        fprintf( stderr, "Failed to create coordinate transformation between the\n"
    //               "following coordinate systems.  This may be because they\n"
    //               "are not transformable, or because projection services\n"
    //               "(PROJ.4 DLL/.so) could not be loaded.\n" );
    //        
    //        poSourceSRS->exportToPrettyWkt( &pszWKT, FALSE );
    //        fprintf( stderr,  "Source:\n%s\n", pszWKT );
    //        
    //        poOutputSRS->exportToPrettyWkt( &pszWKT, FALSE );
    //        fprintf( stderr,  "Target:\n%s\n", pszWKT );
    //        exitMPI( 1 );
    //    }
    //}
    //
    //if (bWrapDateline)
    //{
    //    if( poSourceSRS == NULL )
    //        poSourceSRS = poSrcLayer->GetSpatialRef();

    //    if (poCT != NULL && poOutputSRS->IsGeographic())
    //    {
    //        papszTransformOptions =
    //            CSLAddString(papszTransformOptions, "WRAPDATELINE=YES");
    //    }
    //    else if (poSourceSRS != NULL && poOutputSRS == NULL && poSourceSRS->IsGeographic())
    //    {
    //        papszTransformOptions =
    //            CSLAddString(papszTransformOptions, "WRAPDATELINE=YES");
    //    }
    //    else
    //    {
    //        fprintf(stderr, "-wrapdateline option only works when reprojecting to a geographic SRS\n");
    //    }
    //}
    
/* -------------------------------------------------------------------- */
/*      Get other info.                                                 */
/* -------------------------------------------------------------------- */
    poSrcFDefn = poSrcLayer->GetLayerDefn();

    //if( poOutputSRS == NULL && !bNullifyOutputSRS )
	OGRSpatialReference *poOutputSRS;
    poOutputSRS = poSrcLayer->GetSpatialRef();
	
/* -------------------------------------------------------------------- */
/*      Find the layer. （只能1个进程进行创建，打开用多个进程，创建用一个，在打开用多个）                                                */
/* -------------------------------------------------------------------- */
	
	//panMap[]初始化及赋值
	int         nSrcFieldCount = poSrcFDefn->GetFieldCount();
	int			iField,*panMap;
	panMap = (int *) VSIMalloc( sizeof(int) * nSrcFieldCount );
	for( iField=0; iField < nSrcFieldCount; iField++)
		panMap[iField] = -1;

	if( cp == master)
	{
		OGRFeatureDefn *poDstFDefn = NULL;
		int iLayer = -1;
		poDstLayer = NULL;

		/* GetLayerByName() can instanciate layers that would have been */
		/* 'hidden' otherwise, for example, non-spatial tables in a */
		/* Postgis-enabled database, so this apparently useless command is */
		/* not useless... (#4012) */

		CPLPushErrorHandler(CPLQuietErrorHandler);
		poDstDS->GetLayerByName(pszNewLayerName);
		CPLPopErrorHandler();
		CPLErrorReset();

		for( iLayer = 0; iLayer < poDstDS->GetLayerCount(); iLayer++ )
		{
			OGRLayer        *poLayer = poDstDS->GetLayer(iLayer);

			if( poLayer != NULL 
				&& EQUAL(poLayer->GetName(),pszNewLayerName) )
			{
				poDstLayer = poLayer;
				break;
			}
		}
	    
	/* -------------------------------------------------------------------- */
	/*      If the user requested overwrite, and we have the layer in       */
	/*      question we need to delete it now so it will get recreated      */
	/*      (overwritten).                                                  */
	/* -------------------------------------------------------------------- */
		if( poDstLayer != NULL && bOverwrite )
		{
			if( poDstDS->DeleteLayer( iLayer ) != OGRERR_NONE )
			{
				fprintf( stderr, 
						 "DeleteLayer() failed when overwrite requested.\n" );
				CSLDestroy(papszTransformOptions);
				return FALSE;
			}
			poDstLayer = NULL;
		}

	/* -------------------------------------------------------------------- */
	/*      If the layer does not exist, then create it. （只能单进程进行）                   */
	/* -------------------------------------------------------------------- */

		if( poDstLayer == NULL)
		{
			if( eGType == -2 )
			{
				eGType = poSrcFDefn->GetGeomType();
				//printf("etype is %d\n",eGType);

				//if ( bExplodeCollections )
				//{
				//	int n25DBit = eGType & wkb25DBit;
				//	if (wkbFlatten(eGType) == wkbMultiPoint)
				//	{
				//		eGType = wkbPoint | n25DBit;
				//	}
				//	else if (wkbFlatten(eGType) == wkbMultiLineString)
				//	{
				//		eGType = wkbLineString | n25DBit;
				//	}
				//	else if (wkbFlatten(eGType) == wkbMultiPolygon)
				//	{
				//		eGType = wkbPolygon | n25DBit;
				//	}
				//	else if (wkbFlatten(eGType) == wkbGeometryCollection)
				//	{
				//		eGType = wkbUnknown | n25DBit;
				//	}
				//}
				//else if ( pszZField )
				//	eGType |= wkb25DBit;
			}
			//else
			//{
			//	printf("etype is %d\n",eGType);

			//}

			if( !poDstDS->TestCapability( ODsCCreateLayer ) )
			{
				fprintf( stderr, 
				  "Layer %s not found, and CreateLayer not supported by driver.", 
						 pszNewLayerName );
				return FALSE;
			}

			CPLErrorReset();

			
			poDstLayer = poDstDS->CreateLayer( pszNewLayerName, poOutputSRS,
											   (OGRwkbGeometryType) eGType, 
											   papszLCO );
			//eGType = poDstLayer->GetLayerDefn()->GetGeomType();


			if( poDstLayer == NULL )
			{
				CSLDestroy(papszTransformOptions);
				return FALSE;
			}

			bAppend = FALSE;
			printf(" proc %d have created the layer\n",cp);
			
		}

		/* -------------------------------------------------------------------- */
		/*      Otherwise we will append to it, if append was requested.        */
		/* -------------------------------------------------------------------- */
		else if( !bAppend )
		{
			fprintf( stderr, "FAILED: Layer %s already exists, and -append not specified.\n"
					"        Consider using -append, or -overwrite.\n",
					pszNewLayerName );
			return FALSE;
		}
		else
		{
			if( CSLCount(papszLCO) > 0 )
			{
				fprintf( stderr, "WARNING: Layer creation options ignored since an existing layer is\n"
						"         being appended to.\n" );
			}
		}

/* -------------------------------------------------------------------- */
/*      Process Layer style table                                       */
/* -------------------------------------------------------------------- */

		poDstLayer->SetStyleTable( poSrcLayer->GetStyleTable () );
/* -------------------------------------------------------------------- */
/*      Add fields.  Default to copy all field.                         */
/*      If only a subset of all fields requested, then output only      */
/*      the selected fields, and in the order that they were            */
/*      selected.   （建立目标数据属性字段）  （多个进程）              */
/* -------------------------------------------------------------------- */
		/*int         nSrcFieldCount = poSrcFDefn->GetFieldCount();*/
		/*int         iField*//*, *panMap*/;

		// Initialize the index-to-index map to -1's
		//panMap = (int *) VSIMalloc( sizeof(int) * nSrcFieldCount );
		//for( iField=0; iField < nSrcFieldCount; iField++)
		//	panMap[iField] = -1;
	        
		/* Caution : at the time of writing, the MapInfo driver */
		/* returns NULL until a field has been added */
		poDstFDefn = poDstLayer->GetLayerDefn();

		if (papszSelFields && !bAppend )
		{
			int  nDstFieldCount = 0;
			if (poDstFDefn)
				nDstFieldCount = poDstFDefn->GetFieldCount();
			for( iField=0; papszSelFields[iField] != NULL; iField++)
			{
				int iSrcField = poSrcFDefn->GetFieldIndex(papszSelFields[iField]);
				if (iSrcField >= 0)
				{
					OGRFieldDefn* poSrcFieldDefn = poSrcFDefn->GetFieldDefn(iSrcField);
					OGRFieldDefn oFieldDefn( poSrcFieldDefn );

					if (papszFieldTypesToString != NULL &&
						(CSLFindString(papszFieldTypesToString, "All") != -1 ||
						 CSLFindString(papszFieldTypesToString,
									   OGRFieldDefn::GetFieldTypeName(poSrcFieldDefn->GetType())) != -1))
					{
						oFieldDefn.SetType(OFTString);
					}
	                
					/* The field may have been already created at layer creation */
					int iDstField = -1;
					if (poDstFDefn)
						iDstField = poDstFDefn->GetFieldIndex(oFieldDefn.GetNameRef());
					if (iDstField >= 0)
					{
						panMap[iSrcField] = iDstField;
					}
					else if (poDstLayer->CreateField( &oFieldDefn ) == OGRERR_NONE)
					{
						/* now that we've created a field, GetLayerDefn() won't return NULL */
						if (poDstFDefn == NULL)
							poDstFDefn = poDstLayer->GetLayerDefn();

						/* Sanity check : if it fails, the driver is buggy */
						if (poDstFDefn != NULL &&
							poDstFDefn->GetFieldCount() != nDstFieldCount + 1)
						{
							CPLError(CE_Warning, CPLE_AppDefined,
									 "The output driver has claimed to have added the %s field, but it did not!",
									 oFieldDefn.GetNameRef() );
						}
						else
						{
							panMap[iSrcField] = nDstFieldCount;
							nDstFieldCount ++;
						}
					}
				}
				else
				{
					fprintf( stderr, "Field '%s' not found in source layer.\n", 
							papszSelFields[iField] );
					if( !bSkipFailures )
					{
						VSIFree(panMap);
						CSLDestroy(papszTransformOptions);
						return FALSE;
					}
				}
			}
	        
			/* -------------------------------------------------------------------- */
			/* Use SetIgnoredFields() on source layer if available                  */
			/* -------------------------------------------------------------------- */
			if (poSrcLayer->TestCapability(OLCIgnoreFields))
			{
				int iSrcField;
				char** papszIgnoredFields = NULL;
				int bUseIgnoredFields = TRUE;
				char** papszWHEREUsedFields = NULL;

				if (pszWHERE)
				{
					/* We must not ignore fields used in the -where expression (#4015) */
					OGRFeatureQuery oFeatureQuery;
					if ( oFeatureQuery.Compile( poSrcLayer->GetLayerDefn(), pszWHERE ) == OGRERR_NONE )
					{
						papszWHEREUsedFields = oFeatureQuery.GetUsedFields();
					}
					else
					{
						bUseIgnoredFields = FALSE;
					}
				}

				for(iSrcField=0;iSrcField<poSrcFDefn->GetFieldCount();iSrcField++)
				{
					const char* pszFieldName =
						poSrcFDefn->GetFieldDefn(iSrcField)->GetNameRef();
					int bFieldRequested = FALSE;
					for( iField=0; papszSelFields[iField] != NULL; iField++)
					{
						if (strcmp(pszFieldName, papszSelFields[iField]) == 0)
						{
							bFieldRequested = TRUE;
							break;
						}
					}
					bFieldRequested |= CSLFindString(papszWHEREUsedFields, pszFieldName) >= 0;

					/* If source field not requested, add it to ignored files list */
					if (!bFieldRequested)
						papszIgnoredFields = CSLAddString(papszIgnoredFields, pszFieldName);
				}
				if (bUseIgnoredFields)
					poSrcLayer->SetIgnoredFields((const char**)papszIgnoredFields);
				CSLDestroy(papszIgnoredFields);
				CSLDestroy(papszWHEREUsedFields);
			}
		}
		else if( !bAppend )
		{
			
			int nDstFieldCount = 0;
			if (poDstFDefn)
				nDstFieldCount = poDstFDefn->GetFieldCount();
			//printf("\n proc %d \n ",cp);
			for( iField = 0; iField < nSrcFieldCount; iField++ )
			{
				OGRFieldDefn* poSrcFieldDefn = poSrcFDefn->GetFieldDefn(iField);
				OGRFieldDefn oFieldDefn( poSrcFieldDefn );
				//printf("\n oFieldDefn IS %s \n ",oFieldDefn.GetNameRef());
				if (papszFieldTypesToString != NULL &&
					(CSLFindString(papszFieldTypesToString, "All") != -1 ||
					 CSLFindString(papszFieldTypesToString,
								   OGRFieldDefn::GetFieldTypeName(poSrcFieldDefn->GetType())) != -1))
				{
					oFieldDefn.SetType(OFTString);
				}

				/* The field may have been already created at layer creation */
				int iDstField = -1;
				if (poDstFDefn)
					 iDstField = poDstFDefn->GetFieldIndex(oFieldDefn.GetNameRef());

				if (iDstField >= 0)
				{
					panMap[iField] = iDstField;
				}
				else if (poDstLayer->CreateField( &oFieldDefn ) == OGRERR_NONE)
				{
					
					
					/* now that we've created a field, GetLayerDefn() won't return NULL */
	               
					if (poDstFDefn == NULL)
						poDstFDefn = poDstLayer->GetLayerDefn();

					/* Sanity check : if it fails, the driver is buggy */
					if (poDstFDefn != NULL &&
						poDstFDefn->GetFieldCount() != nDstFieldCount + 1)
					{
						CPLError(CE_Warning, CPLE_AppDefined,
								 "The output driver has claimed to have added the %s field, but it did not!",
								 oFieldDefn.GetNameRef() );
					}
					else
					{
						panMap[iField] = nDstFieldCount;
						nDstFieldCount ++;
					}
				}
			}
			
		}
		else
		{   
			/* For an existing layer, build the map by fetching the index in the destination */
			/* layer for each source field */
			if (poDstFDefn == NULL)
			{
				fprintf( stderr, "poDstFDefn == NULL.\n" );
				VSIFree(panMap);
				CSLDestroy(papszTransformOptions);
				return FALSE;
			}
	        
			for( iField = 0; iField < nSrcFieldCount; iField++ )
			{
				OGRFieldDefn* poSrcFieldDefn = poSrcFDefn->GetFieldDefn(iField);
				int iDstField = poDstFDefn->GetFieldIndex(poSrcFieldDefn->GetNameRef());
				if (iDstField >= 0)
					panMap[iField] = iDstField;
			}
		}

	
		//for(int rank=1;rank<np;rank++)
		//{
			//MPI_Send(panMap,nSrcFieldCount,MPI_INT,rank,MSG_PANMAP,MPI_COMM_WORLD);		
		/*}*/
		//MPI_Bcast(panMap,nSrcFieldCount,MPI_INT,master,MPI_COMM_WORLD);
		//poDstLayer=NULL;
		//printf("proc %d panmap[0] is %d",cp,panMap[0]);\

		OGRDataSource::DestroyDataSource(poDstDS);
		poDstDS=NULL;
			
	}
	MPI_Bcast(panMap,nSrcFieldCount,MPI_INT,master,MPI_COMM_WORLD);
	MPI_Barrier(MPI_COMM_WORLD);//等待




	//int nBlock=3;//定义数据块
	
	long nfeaturescount=0;
	long featureID=-1;//定义feature的编号
	long feaperblock=0;//定义每个数据块处理feature的数量
	long feaPerProcess=0;//定义单个进程处理feature的数量
	long feaStartID=-1;
	long feaEndID=-1;

	nfeaturescount=poSrcLayer->GetFeatureCount();
	feaperblock=nfeaturescount/nBlock;
	feaPerProcess = feaperblock/(np-1);

	
	/* -------------------------------------------------------------------- */
	/*    the true  data conversion                                 */
	/* -------------------------------------------------------------------- */	

	if(cp==master)
	{
		//printf("get message\n");
		for (int i=1;i<=(np-1)*nBlock;)
		{
			MPI_Recv(&msg,1,MPI_INT,MPI_ANY_SOURCE, MSG_FINISHED_TRANSLATING,MPI_COMM_WORLD,&status);
			//printf("get message\n");
			//
			//switch(status.MPI_TAG)
			//{
			//	case MSG_FINISHED_TRANSLATING:
			//	{
			MPI_Send(&msg,1,MPI_INT,status.MPI_SOURCE,MSG_STARTING_WRITING,MPI_COMM_WORLD);
			//printf("proc %d will start writing\n",status.MPI_SOURCE);
			//}
			//case MSG_FINISHED_WRITING:
			//	break;
			/*}*/
			MPI_Recv(&msg,1,MPI_INT,MPI_ANY_SOURCE, MSG_FINISHED_WRITING,MPI_COMM_WORLD,&status);
			//printf("proc %d ended\n",status.MPI_SOURCE);
			i++;
		}
	}
	else
	{
		for(int k=1;k<=nBlock;)
		{
			/* -------------------------------------------------------------------- */
			/*     data partitioning                                    */
			/* -------------------------------------------------------------------- */
			if(EQUAL(pszSFormat, "ESRI Shapefile"))
			{
				feaStartID=feaperblock*(k-1)+feaPerProcess*(cp-1);
				if(cp != np-1)
				{
					feaEndID=feaStartID+feaPerProcess-1;
				}
				else if (k!=nBlock)
				{
					feaEndID=feaperblock*k-1;
				}
				else
				{
					feaEndID=nfeaturescount-1;
				}
			}
			else
			{
				feaStartID=feaperblock*(k-1)+feaPerProcess*(cp-1)+1;
				if(cp!=np-1)
				{
					feaEndID=feaStartID+feaPerProcess-1;
				}
				else if(k!=nBlock)
				{
					feaEndID=feaperblock*k;
				}
				else
				{
					feaEndID=nfeaturescount;
				}
			}


			//printf("block %d,  process %d,  start ID is %d,  end ID is %d\n",k,cp,feaStartID,feaEndID);
			//printf("proc %d,startID is %d,endID is %d\n",cp,feaStartID,feaEndID);
			int         bForceToPolygon = FALSE;
			int         bForceToMultiPolygon = FALSE;
			int         bForceToMultiLineString = FALSE;

			if( wkbFlatten(eGType) == wkbPolygon )
				bForceToPolygon = TRUE;
			else if( wkbFlatten(eGType) == wkbMultiPolygon )
				bForceToMultiPolygon = TRUE;
			else if( wkbFlatten(eGType) == wkbMultiLineString )
				bForceToMultiLineString = TRUE;

			OGRFeature  *poFeature;
			int         nFeaturesInTransaction = 0;
			//long        nCount = 0;

			poSrcLayer->ResetReading();

			//if( nGroupTransactions )
			//	poDstLayer->StartTransaction();
			//printf("begin to open the datasource!\n");
			std::vector <OGRFeature> vec/*(feaEndID-feaStartID+1)*/;
			vec.reserve(feaEndID-feaStartID+1);

			OGRSFDriver *poDriver=NULL;
			OGRDataSource *poDestDatasource=NULL;
			poDestDatasource= OGRSFDriverRegistrar::Open(pszDestDataSource/*"D:\\GDALdata\test\\JQDLTB.tab"*/,TRUE,&poDriver);
			//if (poDriver!=NULL)
			//{
			//	printf("dest driver is %s\n",poDriver->GetName());
			//}
			//poDestDatasource=poDriver->Open(pszDestDataSource,FALSE);
			//if (poDestDatasource==NULL)
			//{
			//	printf("proc %d open error\n",cp);
			//}
			poDstLayer=poDestDatasource->GetLayerByName(pszNewLayerName);		
			if(poDstLayer!=NULL)
			{
				//printf("\n proc %d have opened the layer\n ",cp);
				//time3 = MPI_Wtime();
				/* -------------------------------------------------------------------- */
				/*     feature translate                                      */
				/* -------------------------------------------------------------------- */
				for(featureID = feaStartID;featureID <= feaEndID;featureID++)
				{
					OGRFeature      *poDstFeature = NULL;
					if( nFIDToFetch != OGRNullFID )
					{
						// Only fetch feature on first pass.
						if( nFeaturesInTransaction == 0 )
							poFeature = poSrcLayer->GetFeature(nFIDToFetch);				
						else
							poFeature = NULL;
					}
					else
						poFeature = poSrcLayer->GetFeature(featureID);


					if( poFeature == NULL )
					{
						break;		
					}
					int nParts = 0;
					int nIters = 1;
					//if (bExplodeCollections)
					//{
					//	OGRGeometry* poSrcGeometry = poFeature->GetGeometryRef();
					//	if (poSrcGeometry)
					//	{
					//		switch (wkbFlatten(poSrcGeometry->getGeometryType()))
					//		{
					//			case wkbMultiPoint:
					//			case wkbMultiLineString:
					//			case wkbMultiPolygon:
					//			case wkbGeometryCollection:
					//				nParts = ((OGRGeometryCollection*)poSrcGeometry)->getNumGeometries();
					//				nIters = nParts;
					//				if (nIters == 0)
					//					nIters = 1;
					//			default:
					//				break;
					//		}
					//	}
					//}


					for(int iPart = 0; iPart < nIters; iPart++)
					{
						//if( ++nFeaturesInTransaction == nGroupTransactions )
						//{
						//	poDstLayer->CommitTransaction();
						//	poDstLayer->StartTransaction();
						//	nFeaturesInTransaction = 0;
						//}

						CPLErrorReset();

						poDstFeature = OGRFeature::CreateFeature( poDstLayer->GetLayerDefn() );
						if( poDstFeature->SetFrom( poFeature, panMap, TRUE ) != OGRERR_NONE )
						{
							//if( nGroupTransactions )
							//	poDstLayer->CommitTransaction();

							CPLError( CE_Failure, CPLE_AppDefined,
								"Unable to translate feature %ld from layer %s.\n",
								poFeature->GetFID(), poSrcFDefn->GetName() );

							OGRFeature::DestroyFeature( poFeature );
							OGRFeature::DestroyFeature( poDstFeature );
							VSIFree(panMap);
							CSLDestroy(papszTransformOptions);
							return FALSE;
						}

						if( bPreserveFID )
						{
							poDstFeature->SetFID( poFeature->GetFID() );
						}

						OGRGeometry* poDstGeometry = poDstFeature->GetGeometryRef();
						if (poDstGeometry != NULL)
						{
							//printf("\n proc %d \n ",cp);
							if (nParts > 0)
							{
								/* For -explodecollections, extract the iPart(th) of the geometry */
								OGRGeometry* poPart = ((OGRGeometryCollection*)poDstGeometry)->getGeometryRef(iPart);
								((OGRGeometryCollection*)poDstGeometry)->removeGeometry(iPart, FALSE);
								poDstFeature->SetGeometryDirectly(poPart);
								poDstGeometry = poPart;
							}

							if (poClipSrc)
							{
								OGRGeometry* poClipped = poDstGeometry->Intersection(poClipSrc);
								if (poClipped == NULL || poClipped->IsEmpty())
								{
									OGRGeometryFactory::destroyGeometry(poClipped);
									goto end_loop;
								}
								poDstFeature->SetGeometryDirectly(poClipped);
								poDstGeometry = poClipped;
							}

							poDstGeometry->assignSpatialReference(poOutputSRS);

							if( bForceToPolygon )
							{
								poDstFeature->SetGeometryDirectly(
									OGRGeometryFactory::forceToPolygon(
									poDstFeature->StealGeometry() ) );
							}
							else if( bForceToMultiPolygon )
							{
								poDstFeature->SetGeometryDirectly(
									OGRGeometryFactory::forceToMultiPolygon(
									poDstFeature->StealGeometry() ) );
							}
							else if ( bForceToMultiLineString )
							{
								poDstFeature->SetGeometryDirectly(
									OGRGeometryFactory::forceToMultiLineString(
									poDstFeature->StealGeometry() ) );
							}
						}
						CPLErrorReset();

						vec.push_back(*(poDstFeature->Clone()));				
end_loop:
						OGRFeature::DestroyFeature( poDstFeature );
					}

					OGRFeature::DestroyFeature( poFeature );
				}
				//time4 = MPI_Wtime();
				//printf("proc %d,the feature translating time is %f seconds\n",cp,(time4-time3));
			}
			OGRDataSource::DestroyDataSource(poDestDatasource);
			MPI_Send(&msg,1,MPI_INT,master,MSG_FINISHED_TRANSLATING,MPI_COMM_WORLD);
			//printf("proc %d has finished translating\n",cp);
			//if( nGroupTransactions )
			//	poDstLayer->CommitTransaction();
			//printf("proc %d,vector's size is %d\n",cp,vec.size ());
			/* -------------------------------------------------------------------- */
			/*    reopen the datasource                                    */
			/* -------------------------------------------------------------------- */
			MPI_Recv(&msg,1,MPI_INT,master,MSG_STARTING_WRITING,MPI_COMM_WORLD,&status);
			//printf("proc %d will reopen the layer and start to write\n",cp);
			OGRDataSource *poDestDS=NULL;
			//poDestDS= OGRSFDriverRegistrar::Open(pszDestDataSource/*"D:\\GDALdata\test\\JQDLTB.tab"*/,TRUE,NULL);
			poDestDS=poDriver->Open(pszDestDataSource,TRUE);
			poDstLayer=poDestDS->GetLayerByName(pszNewLayerName);		
			if(poDstLayer!=NULL)
			{
				//printf("\n proc %d have opened the layer\n ",cp);
				//time5 = MPI_Wtime();
				//printf("proc %d,the waiting time is %f seconds\n",cp,(time5-time4));
				/* -------------------------------------------------------------------- */
				/*    feature writing                                   */
				/* -------------------------------------------------------------------- */
				for (int ix=0; ix!=(int)vec.size(); ++ix)  
				{ 
					//OGRFeature ipoly=vec[ix];
					//printf("feature %d\n",ix);
					//printf("poDestFeature ID is %d\n",vec[ix].GetFID());
					//printf("fieldcount is %d\n",vec[ix].GetFieldCount());
					//OGRFeature *poDestFeature=ipoly.Clone ();
					if( poDstLayer->CreateFeature(&vec[ix]) != OGRERR_NONE
						&& !bSkipFailures )
					{
						//if( nGroupTransactions )
						//poDstLayer->RollbackTransaction();
						//printf("111 \n");

						//OGRFeature::DestroyFeature( poFeature );
						//OGRFeature::DestroyFeature( poDestFeature );
						//delete *iter; 
						VSIFree(panMap);
						CSLDestroy(papszTransformOptions);
						return FALSE; 
					}

				}
				//time6 = MPI_Wtime();
				//printf("proc %d,the feature writing time is %f seconds\n",cp,(time6-time5));
			}
			OGRDataSource::DestroyDataSource(poDestDS); 
			vec.clear();
			MPI_Send(&msg,1,MPI_INT,master,MSG_FINISHED_WRITING,MPI_COMM_WORLD);
			//printf("proc %d has finished writing\n",cp);
			k++;
		}
	}
	

/* -------------------------------------------------------------------- */
/*      Cleaning                                                        */
/* -------------------------------------------------------------------- */
    //OGRCoordinateTransformation::DestroyCT(poCT);
	/*OGRDataSource::DestroyDataSource(poDestDatasource);*/
    //OGRDataSource::DestroyDataSource(poDstDS);
    VSIFree(panMap);
    CSLDestroy(papszTransformOptions);

    return TRUE;
}
