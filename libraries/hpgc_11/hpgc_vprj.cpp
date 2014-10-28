#include "gt_geometry.h"
#include "gt_spatialindex.h"
#include "gt_datasource.h"
#include  "gt_datadriver.h"
#include "ogrsf_frmts.h"
#include "ogr_feature.h"
#include "gt_spatialreference.h"
#include "ogr_geometry.h"
#include "mpi.h"
#include <iostream>

using namespace std;
int main( int nArgc, char *papszArgv[] )
{	
	OGRRegisterAll();
	//****************************//
	//*******参数解析********//	
	//****************************//
	char *pszSrcFilename=NULL;
	char *layername = NULL;
	char *newlayer = NULL;
	const char  *pszsrsputSRSDef = NULL;
	const char  *pszOutputSRSDef = NULL;
	OGRSpatialReference *ogrsrsputSRS = (OGRSpatialReference*)OSRNewSpatialReference(NULL);	//存储原投影
	OGRSpatialReference *ogrOutputSRS = (OGRSpatialReference*)OSRNewSpatialReference(NULL);	//存储目标投影	
	gts::GTGDOSMySQLDataSource *pSrc= new GTGDOSMySQLDataSource();
	char* oHost=NULL,*oPassword=NULL,*oUser=NULL,*oDB=NULL, **papszTableNames=NULL;
	int nPort=0,j=0;
	/*papszArgv[0]="hpgc_defineprj";
	papszArgv[1]="-l";	
	papszArgv[2]="new_point6";
	papszArgv[3]="-nl";
	papszArgv[4]="newpoint11";
	papszArgv[5]="-t_srs";
	papszArgv[6]="EPSG:4326";
	papszArgv[7]="MYSQL:testing_mysql2,user=root,password=123456,host=localhost,port=3306";
	nArgc=8;*/

	nArgc = OGRGeneralCmdLineProcessor( nArgc, &papszArgv, 0 );
	if( nArgc < 1 )
	{
		MPI_Finalize();
        return 0;
	}

	for( int iArg = 1; iArg < nArgc; iArg++ )
	{
		if( EQUAL(papszArgv[iArg], "-l") )
		{
			layername = papszArgv[++iArg];
			if (layername==NULL)
			{
				printf("Get source layer name failed");
				
				return 0 ;
			}	

		}
		else if( EQUAL(papszArgv[iArg], "-nl") )
		{
			newlayer = papszArgv[++iArg];
			if (newlayer==NULL)
			{
				printf("Get target layer name failed");
				
				return 0 ;
			}	

		}
		else if ( EQUAL(papszArgv[iArg], "-a_srs") )
		{
			pszsrsputSRSDef = papszArgv[++iArg];
			ogrsrsputSRS->SetFromUserInput(pszsrsputSRSDef );
			if (ogrsrsputSRS==NULL)
			{
				printf("Get source srs failed");
				
				return 0;
			}			
		}

		else if ( EQUAL(papszArgv[iArg], "-t_srs") )
		{
			pszOutputSRSDef = papszArgv[++iArg];
			ogrOutputSRS->SetFromUserInput( pszOutputSRSDef );			
			if (ogrOutputSRS==NULL)
			{
				printf("Get target srs failed");
				
				return 0;
			}			
		}

		else if( pszSrcFilename==NULL)
		{
			pszSrcFilename = papszArgv[iArg];
			if (pszSrcFilename==NULL)
			{
				printf("Get src failed");
				
				return 0;
			}						
		}
	}
	
	
	//****************************//
	//*******数据库参数解析********//	
	//****************************//
	string pstr(pszSrcFilename);
	if(EQUAL(pstr.substr(0,6).c_str(),"MYSQL:"))
	{//try to get the srs of a gdao-mysql-datasource
				
		char **papszItems = CSLTokenizeString2( pszSrcFilename+6, ",", CSLT_HONOURSTRINGS );
		if( CSLCount(papszItems) < 1 )
		{
			CSLDestroy( papszItems );
			CPLError( CE_Failure, CPLE_AppDefined, "MYSQL: request missing databasename." );
			
			return 0;
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
							"'%s' in MYSQL datasource definition not recognised and ignored.", papszItems[j] );					

		}
	}	

	//****************************//
	//*******打开数据源*********//	
	//****************************//
	
	
	
	bool isOpen = pSrc->openDataSource("Mysql",oUser,oPassword,oHost,nPort,oDB);//打开已存储原数据的数据库
	if (!isOpen)//判断是否打开数据源数据库
	{
		printf( "open datasource failed..\n");
		GTGDOSMySQLDataSource::destroyDataSource(pSrc);
		return 0;
	}	
	
	GTFeatureLayer *pSrcLyr = pSrc->getFeatureLayerByName(layername, false);//从数据库中通过图层名取出图层，名称为point,false=update
	if (!pSrcLyr)
	{
		printf( "open layer failed..");
		GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
		GTGDOSMySQLDataSource::destroyDataSource(pSrc);
		return 0;
	}
	//****************************//
	//*******获取空间参考*********//	
	//****************************//
	GTSpatialReference *poSourceSRS = new GTSpatialReference();
	

    poSourceSRS = pSrcLyr->getLayerSpatialRefPtr();//获取源投影空间参考（输入）        			
	
	//****************************//
	//*******建立OGR空间参考*********//	
	//****************************//

	OGRCoordinateTransformation *poCT = NULL;		
	OGRSpatialReference *ogrSourceSRS =(OGRSpatialReference*)OSRNewSpatialReference(NULL);	
	ogrSourceSRS=poSourceSRS->getOGRSpatialRefPtr();//Get sourceSRS

	if (pszsrsputSRSDef!=NULL && ogrSourceSRS!=NULL)
	{
		printf("source data already had projection,pleace not define new projection.\n" );
		GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
		GTGDOSMySQLDataSource::destroyDataSource(pSrc);
		return 0;
	}

	if(ogrSourceSRS==NULL)
	{
		printf("Get sourceSRS failed\n");			
		GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
		GTGDOSMySQLDataSource::destroyDataSource(pSrc);
		return 0;
	}
	//Get outputSRS
	if(ogrOutputSRS==NULL)
	{
		printf("Get OutputSRS failed\n");
		GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
		GTGDOSMySQLDataSource::destroyDataSource(pSrc);
		return 0;
	}
	if(pszsrsputSRSDef!=NULL)
		poCT = OGRCreateCoordinateTransformation( ogrsrsputSRS, ogrOutputSRS );//Create Coordinate Transformation
	else
		poCT = OGRCreateCoordinateTransformation( ogrSourceSRS, ogrOutputSRS );
	if (poCT == NULL)
	{
		printf("Create Coordinate Transformation failed\n");
		GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
		GTGDOSMySQLDataSource::destroyDataSource(pSrc);
		return 0;
	}

	//*******设置转换参数*********//
	char**      papszTransformOptions = NULL;
	 if (poCT != NULL && ogrOutputSRS->IsGeographic())
    {
        papszTransformOptions =
            CSLAddString(papszTransformOptions, "WRAPDATELINE=YES");
    }
    else if (poSourceSRS != NULL && ogrOutputSRS == NULL && ogrSourceSRS->IsGeographic())
    {
        papszTransformOptions =
            CSLAddString(papszTransformOptions, "WRAPDATELINE=YES");
    }
    else
    {
       // fprintf(stderr, "-wrapdateline option only works when reprojecting to a geographic SRS\n");
    }
	//printf("获取转换参数、\n");

	//****************************//
	//****Create parallel mpi*****//	
	//****************************//  
	int mpi_rank, mpi_size;
	
	MPI_Init(&nArgc, &papszArgv);
	MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);
	
	//****************************//
	//*******创建输出图层*********//	
	//****************************//
	pSrc->deleteFeatureLayerByName(newlayer);//delete haved layer
	GTFeatureLayer *pDrcLyr=new GTFeatureLayer();
	if(mpi_rank==0)
	{
		pDrcLyr = pSrc->createFeatureLayer(newlayer,NULL,pSrcLyr->getFieldsDefnPtr(),pSrcLyr->getGeometryType());	//创建输出数据层
		char **srsinfo = NULL;
		srsinfo=(char**)malloc(1000*sizeof(char));
			for (int i = 0;i< 1000 ;i++ )
		{
			*srsinfo = (char*) malloc(sizeof(char)*1000);
		}
		ogrOutputSRS->exportToWkt(srsinfo);			
		pSrc->defineLayerSpatialReference(newlayer,*srsinfo);		
		if (!pDrcLyr)
		{
			printf( "Create layer failed..");
			
			GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
			GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
			GTGDOSMySQLDataSource::destroyDataSource(pSrc);
			MPI_Finalize();
			return 0;
		}
	
		//****************************//
		//*******建立投影转换*********//	
		//****************************//       
		//写入属性数据	
		long N_count = pSrcLyr->getFieldsDefnPtr()->getFieldCount();
	
		for (long f = 0; f < N_count; f++)
		{
			bool isFail = pDrcLyr->createField(pSrcLyr->getFieldsDefnPtr()->getFieldPtr(f));
			if(isFail)
			{
				printf("Create Field failed\n");
				GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
				GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
				GTGDOSMySQLDataSource::destroyDataSource(pSrc);
				MPI_Finalize();
				return 0;
			}
		}
		//GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
	}
	GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
	MPI_Barrier(MPI_COMM_WORLD);
	pDrcLyr = pSrc->getFeatureLayerByName(newlayer,true);
	if (!pDrcLyr)
	{
		printf( "get new layer failed..");
		GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
		MPI_Finalize();
		return 0;
	}
	//printf("create attribute table successfully.\n");
	

	//写入图形数据
	long feature_count = pSrcLyr->getFeatureCount();//get all feature count
	long feature_N = 0;//define every rank uses feature count
	long feature_S = 0;//start feature NUM
	long feature_E = 0;//end feature NUM
	if (mpi_size > feature_count && feature_count>0)//mpi's processers more than feature count
	{
		feature_S = mpi_rank;
		feature_E = mpi_rank+1;
	}
	else if(mpi_size < feature_count)
	{
		feature_N = feature_count/mpi_size;
		feature_S = mpi_rank*feature_N;

		if(mpi_rank == mpi_size-1)
		feature_E = feature_count;
		else
			feature_E = feature_S+feature_N;
	}
	double start_time = 0,end_time = 0;
	start_time = MPI_Wtime();
	for (feature_S; feature_S < feature_E; ++feature_S)//use while .......
	{
		GTFeature *add_Feat = pSrcLyr->getFeature(feature_S);//创建原feature变量		
		GTGeometry *SrsGeometry = add_Feat->getGeometryPtr()->clone();
		
		/* 定义编码类型顺序,WKBXDR = 0,MSB/Sun/Motoroloa: Most Significant Byte First big-endian
		WKBNDR = 1,LSB/Intel/Vax: Least Significant Byte First little-endian*/			
		enumGTWKBByteOrder byteOrder = WKBNDR;
		OGRwkbByteOrder byteOrder2 = wkbNDR;

		/********判断输出数据类型*********/	
		enumGTWKBGeometryType p_type=SrsGeometry->getGeometryWKBType();//获取数据类型

		/********数据类型为Point*********/	
		if( p_type==1)
		{
			OGRPoint* DestGeom = new OGRPoint();
			GTPoint * Destgeometry=new GTPoint();

			int nsize = SrsGeometry->getWKBSize();//获取图形字节
			unsigned char* Datain =(unsigned char*)malloc(nsize);//预定义开辟空间
			unsigned char* Dataout=(unsigned char*)malloc(nsize);//预定义开辟空间
			SrsGeometry->exportToWkb(byteOrder,Datain);	//gt->wkb
			DestGeom->importFromWkb(Datain);//wkb->ogr
			if(DestGeom==NULL)
			{
				printf("no geom\n");
			}

			DestGeom->assignSpatialReference(ogrSourceSRS);//give geometry to assign a spatialreference

			if(poCT != NULL || papszTransformOptions != NULL)
			{
				//OGRGeometry* poReprojectedGeom = OGRGeometryFactory::transformWithOptions(DestGeom, poCT, papszTransformOptions);//投影转换
				
				//printf("Before:%f\n",DestGeom->getX());
				bool isFalse = DestGeom->transform(poCT);//投影转换			
				//printf("After:%f\n",DestGeom->getX());		
				if( isFalse )
				 {
					fprintf( stderr, "Failed to reproject feature %d (geometry probably out of source or destination SRS).\n", feature_S );
					GTFeature::destroyFeature(add_Feat);
					GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
					GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
					GTGDOSMySQLDataSource::destroyDataSource(pSrc);
					delete []Datain;
					delete []Dataout;
					MPI_Finalize();
					return 0;
				 }			
				DestGeom->exportToWkb(byteOrder2,Dataout);//ogr->wkb
				Destgeometry->importFromWkb(Dataout);//wkb->gt		
				add_Feat->setGeometryDirectly(Destgeometry);			
				bool isSuccess = pDrcLyr->createFeature(add_Feat);
				if(!isSuccess)
				{
					printf("create feature failed.\n");
					GTFeature::destroyFeature(add_Feat);
					GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
					GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
					GTGDOSMySQLDataSource::destroyDataSource(pSrc);
					delete []Datain;
					delete []Dataout;
					MPI_Finalize();
					return 0;
				}
		
					//GTFeature::destroyFeature(Dest_feature);
					//GTFeature::destroyFeature(add_Feat);
				
			}
			delete []Datain;
			delete []Dataout;		
		}
		/********数据类型为LineString*********/	
		else if( p_type==2)
		{
			OGRLineString* DestGeom = new OGRLineString();
			GTLineString * Destgeometry=new GTLineString();

			int nsize = SrsGeometry->getWKBSize();//获取图形字节
			unsigned char* Datain =(unsigned char*)malloc(nsize);//预定义开辟空间
			unsigned char* Dataout=(unsigned char*)malloc(nsize);//预定义开辟空间
			SrsGeometry->exportToWkb(byteOrder,Datain);	//gt->wkb
			DestGeom->importFromWkb(Datain);//wkb->ogr
			if(DestGeom==NULL)
			{
				printf("no geom\n");
			}

			DestGeom->assignSpatialReference(ogrSourceSRS);//give geometry to assign a spatialreference

			if(poCT != NULL || papszTransformOptions != NULL)
			{
				//OGRGeometry* poReprojectedGeom = OGRGeometryFactory::transformWithOptions(DestGeom, poCT, papszTransformOptions);//投影转换
				
				//printf("Before:%f\n",DestGeom->getX());
				bool isFalse = DestGeom->transform(poCT);//投影转换			
				//printf("After:%f\n",DestGeom->getX());		
				if( isFalse )
				 {
					fprintf( stderr, "Failed to reproject feature %d (geometry probably out of source or destination SRS).\n", feature_S );
					GTFeature::destroyFeature(add_Feat);
					GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
					GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
					GTGDOSMySQLDataSource::destroyDataSource(pSrc);
					delete []Datain;
					delete []Dataout;
					MPI_Finalize();
					return 0;
				 }			
				DestGeom->exportToWkb(byteOrder2,Dataout);//ogr->wkb
				Destgeometry->importFromWkb(Dataout);//wkb->gt		
				add_Feat->setGeometryDirectly(Destgeometry);			
				bool isSuccess = pDrcLyr->createFeature(add_Feat);
				if(!isSuccess)
				{
					printf("create feature failed.\n");
					GTFeature::destroyFeature(add_Feat);
					GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
					GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
					GTGDOSMySQLDataSource::destroyDataSource(pSrc);
					delete []Datain;
					delete []Dataout;
					MPI_Finalize();
					return 0;
				}
		
					//GTFeature::destroyFeature(Dest_feature);
					//GTFeature::destroyFeature(add_Feat);
				
			}
			delete []Datain;
			delete []Dataout;		
		}
		/********数据类型为Polygon*********/	
		else if( p_type==3)
		{
			OGRPolygon* DestGeom = new OGRPolygon();
			GTPolygon * Destgeometry=new GTPolygon();

			int nsize = SrsGeometry->getWKBSize();//获取图形字节
			unsigned char* Datain =(unsigned char*)malloc(nsize);//预定义开辟空间
			unsigned char* Dataout=(unsigned char*)malloc(nsize);//预定义开辟空间
			SrsGeometry->exportToWkb(byteOrder,Datain);	//gt->wkb
			DestGeom->importFromWkb(Datain);//wkb->ogr
			if(DestGeom==NULL)
			{
				printf("no geom\n");
			}

			DestGeom->assignSpatialReference(ogrSourceSRS);//give geometry to assign a spatialreference

			if(poCT != NULL || papszTransformOptions != NULL)
			{
				//OGRGeometry* poReprojectedGeom = OGRGeometryFactory::transformWithOptions(DestGeom, poCT, papszTransformOptions);//投影转换
				
				//printf("Before:%f\n",DestGeom->getX());
				bool isFalse = DestGeom->transform(poCT);//投影转换			
				//printf("After:%f\n",DestGeom->getX());		
				if( isFalse )
				 {
					fprintf( stderr, "Failed to reproject feature %d (geometry probably out of source or destination SRS).\n", feature_S );
					GTFeature::destroyFeature(add_Feat);
					GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
					GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
					GTGDOSMySQLDataSource::destroyDataSource(pSrc);
					delete []Datain;
					delete []Dataout;
					MPI_Finalize();
					return 0;
				 }			
				DestGeom->exportToWkb(byteOrder2,Dataout);//ogr->wkb
				Destgeometry->importFromWkb(Dataout);//wkb->gt		
				add_Feat->setGeometryDirectly(Destgeometry);			
				bool isSuccess = pDrcLyr->createFeature(add_Feat);
				if(!isSuccess)
				{
					printf("create feature failed.\n");
					GTFeature::destroyFeature(add_Feat);
					GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
					GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
					GTGDOSMySQLDataSource::destroyDataSource(pSrc);
					delete []Datain;
					delete []Dataout;
					MPI_Finalize();
					return 0;
				}
		
					//GTFeature::destroyFeature(Dest_feature);
					//GTFeature::destroyFeature(add_Feat);
				
			}
			delete []Datain;
			delete []Dataout;		
		}
		/********数据类型为MultiPoint*********/	
		else if( p_type==4)
		{
			OGRMultiPoint* DestGeom = new OGRMultiPoint();
			GTMultiPoint * Destgeometry=new GTMultiPoint();

			int nsize = SrsGeometry->getWKBSize();//获取图形字节
			unsigned char* Datain =(unsigned char*)malloc(nsize);//预定义开辟空间
			unsigned char* Dataout=(unsigned char*)malloc(nsize);//预定义开辟空间
			SrsGeometry->exportToWkb(byteOrder,Datain);	//gt->wkb
			DestGeom->importFromWkb(Datain);//wkb->ogr
			if(DestGeom==NULL)
			{
				printf("no geom\n");
			}

			DestGeom->assignSpatialReference(ogrSourceSRS);//give geometry to assign a spatialreference

			if(poCT != NULL || papszTransformOptions != NULL)
			{
				//OGRGeometry* poReprojectedGeom = OGRGeometryFactory::transformWithOptions(DestGeom, poCT, papszTransformOptions);//投影转换
				
				//printf("Before:%f\n",DestGeom->getX());
				bool isFalse = DestGeom->transform(poCT);//投影转换			
				//printf("After:%f\n",DestGeom->getX());		
				if( isFalse )
				 {
					fprintf( stderr, "Failed to reproject feature %d (geometry probably out of source or destination SRS).\n", feature_S );
					GTFeature::destroyFeature(add_Feat);
					GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
					GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
					GTGDOSMySQLDataSource::destroyDataSource(pSrc);
					delete []Datain;
					delete []Dataout;
					MPI_Finalize();
					return 0;
				 }			
				DestGeom->exportToWkb(byteOrder2,Dataout);//ogr->wkb
				Destgeometry->importFromWkb(Dataout);//wkb->gt		
				add_Feat->setGeometryDirectly(Destgeometry);			
				bool isSuccess = pDrcLyr->createFeature(add_Feat);
				if(!isSuccess)
				{
					printf("create feature failed.\n");
					GTFeature::destroyFeature(add_Feat);
					GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
					GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
					GTGDOSMySQLDataSource::destroyDataSource(pSrc);
					delete []Datain;
					delete []Dataout;
					MPI_Finalize();
					return 0;
				}
		
					//GTFeature::destroyFeature(Dest_feature);
					//GTFeature::destroyFeature(add_Feat);
				
			}
			delete []Datain;
			delete []Dataout;		
		}

		/********数据类型为MultiLineString*********/	
		else if( p_type==5)
		{
			OGRMultiLineString* DestGeom = new OGRMultiLineString();
			GTMultiLineString * Destgeometry=new GTMultiLineString();

			int nsize = SrsGeometry->getWKBSize();//获取图形字节
			unsigned char* Datain =(unsigned char*)malloc(nsize);//预定义开辟空间
			unsigned char* Dataout=(unsigned char*)malloc(nsize);//预定义开辟空间
			SrsGeometry->exportToWkb(byteOrder,Datain);	//gt->wkb
			DestGeom->importFromWkb(Datain);//wkb->ogr
			if(DestGeom==NULL)
			{
				printf("no geom\n");
			}

			DestGeom->assignSpatialReference(ogrSourceSRS);//give geometry to assign a spatialreference

			if(poCT != NULL || papszTransformOptions != NULL)
			{
				//OGRGeometry* poReprojectedGeom = OGRGeometryFactory::transformWithOptions(DestGeom, poCT, papszTransformOptions);//投影转换
				
				//printf("Before:%f\n",DestGeom->getX());
				bool isFalse = DestGeom->transform(poCT);//投影转换			
				//printf("After:%f\n",DestGeom->getX());		
				if( isFalse )
				 {
					fprintf( stderr, "Failed to reproject feature %d (geometry probably out of source or destination SRS).\n", feature_S );
					GTFeature::destroyFeature(add_Feat);
					GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
					GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
					GTGDOSMySQLDataSource::destroyDataSource(pSrc);
					delete []Datain;
					delete []Dataout;
					MPI_Finalize();
					return 0;
				 }			
				DestGeom->exportToWkb(byteOrder2,Dataout);//ogr->wkb
				Destgeometry->importFromWkb(Dataout);//wkb->gt		
				add_Feat->setGeometryDirectly(Destgeometry);			
				bool isSuccess = pDrcLyr->createFeature(add_Feat);
				if(!isSuccess)
				{
					printf("create feature failed.\n");
					GTFeature::destroyFeature(add_Feat);
					GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
					GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
					GTGDOSMySQLDataSource::destroyDataSource(pSrc);
					delete []Datain;
					delete []Dataout;
					MPI_Finalize();
					return 0;
				}
		
					//GTFeature::destroyFeature(Dest_feature);
					//GTFeature::destroyFeature(add_Feat);
				
			}
			delete []Datain;
			delete []Dataout;		
		}

		/********数据类型为MultiPolygon*********/	
		else if( p_type==6)
		{
			OGRMultiPolygon* DestGeom = new OGRMultiPolygon();
			GTMultiPolygon * Destgeometry=new GTMultiPolygon();

			int nsize = SrsGeometry->getWKBSize();//获取图形字节
			unsigned char* Datain =(unsigned char*)malloc(nsize);//预定义开辟空间
			unsigned char* Dataout=(unsigned char*)malloc(nsize);//预定义开辟空间
			SrsGeometry->exportToWkb(byteOrder,Datain);	//gt->wkb
			DestGeom->importFromWkb(Datain);//wkb->ogr
			if(DestGeom==NULL)
			{
				printf("no geom\n");
			}

			DestGeom->assignSpatialReference(ogrSourceSRS);//give geometry to assign a spatialreference

			if(poCT != NULL || papszTransformOptions != NULL)
			{
				//OGRGeometry* poReprojectedGeom = OGRGeometryFactory::transformWithOptions(DestGeom, poCT, papszTransformOptions);//投影转换
				
				//printf("Before:%f\n",DestGeom->getX());
				bool isFalse = DestGeom->transform(poCT);//投影转换			
				//printf("After:%f\n",DestGeom->getX());		
				if( isFalse )
				 {
					fprintf( stderr, "Failed to reproject feature %d (geometry probably out of source or destination SRS).\n", feature_S );
					GTFeature::destroyFeature(add_Feat);
					GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
					GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
					GTGDOSMySQLDataSource::destroyDataSource(pSrc);
					delete []Datain;
					delete []Dataout;
					MPI_Finalize();
					return 0;
				 }			
				DestGeom->exportToWkb(byteOrder2,Dataout);//ogr->wkb
				Destgeometry->importFromWkb(Dataout);//wkb->gt		
				add_Feat->setGeometryDirectly(Destgeometry);			
				bool isSuccess = pDrcLyr->createFeature(add_Feat);
				if(!isSuccess)
				{
					printf("create feature failed.\n");
					GTFeature::destroyFeature(add_Feat);
					GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
					GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
					GTGDOSMySQLDataSource::destroyDataSource(pSrc);
					delete []Datain;
					delete []Dataout;
					MPI_Finalize();
					return 0;
				}
		
					//GTFeature::destroyFeature(Dest_feature);
					//GTFeature::destroyFeature(add_Feat);
				
			}
			delete []Datain;
			delete []Dataout;		
		}

		/********数据类型为GeometryCollection*********/	
		else if( p_type==7)
		{
			OGRGeometryCollection* DestGeom = new OGRGeometryCollection();
			GTGeometryBag * Destgeometry=new GTGeometryBag();

			int nsize = SrsGeometry->getWKBSize();//获取图形字节
			unsigned char* Datain =(unsigned char*)malloc(nsize);//预定义开辟空间
			unsigned char* Dataout=(unsigned char*)malloc(nsize);//预定义开辟空间
			SrsGeometry->exportToWkb(byteOrder,Datain);	//gt->wkb
			DestGeom->importFromWkb(Datain);//wkb->ogr
			if(DestGeom==NULL)
			{
				printf("no geom\n");
			}

			DestGeom->assignSpatialReference(ogrSourceSRS);//give geometry to assign a spatialreference

			if(poCT != NULL || papszTransformOptions != NULL)
			{
				//OGRGeometry* poReprojectedGeom = OGRGeometryFactory::transformWithOptions(DestGeom, poCT, papszTransformOptions);//投影转换
				
				//printf("Before:%f\n",DestGeom->getX());
				bool isFalse = DestGeom->transform(poCT);//投影转换			
				//printf("After:%f\n",DestGeom->getX());		
				if( isFalse )
				 {
					fprintf( stderr, "Failed to reproject feature %d (geometry probably out of source or destination SRS).\n", feature_S );
					GTFeature::destroyFeature(add_Feat);
					GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
					GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
					GTGDOSMySQLDataSource::destroyDataSource(pSrc);
					delete []Datain;
					delete []Dataout;
					MPI_Finalize();
					return 0;
				 }			
				DestGeom->exportToWkb(byteOrder2,Dataout);//ogr->wkb
				Destgeometry->importFromWkb(Dataout);//wkb->gt		
				add_Feat->setGeometryDirectly(Destgeometry);			
				bool isSuccess = pDrcLyr->createFeature(add_Feat);
				if(!isSuccess)
				{
					printf("create feature failed.\n");
					GTFeature::destroyFeature(add_Feat);
					GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
					GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
					GTGDOSMySQLDataSource::destroyDataSource(pSrc);
					delete []Datain;
					delete []Dataout;
					MPI_Finalize();
					return 0;
				}
		
					//GTFeature::destroyFeature(Dest_feature);
					//GTFeature::destroyFeature(add_Feat);
				
			}
			delete []Datain;
			delete []Dataout;		
		}
		else
			printf("Other data type,not support.\n");
	/********其它数据类型*********/
	/*********有待扩充***********/
	/********其它数据类型*********/


	}//循环末点
	MPI_Barrier(MPI_COMM_WORLD);
	end_time = MPI_Wtime();
	double t = end_time - start_time;
	if(mpi_rank==0)
	{
		printf("Time = %f \n",t);
		printf("start_time=%f\n",start_time);
		printf("end_time=%f\n",end_time);
	}

	//printf("create feature successfully.\n");
	GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
	GTFeatureLayer::destroyFeatureLayer(pDrcLyr);
	CSLDestroy(papszTransformOptions);
	pSrc->closeDataSource();
	GTGDOSMySQLDataSource::destroyDataSource(pSrc);
	MPI_Finalize();
	return 0;
}
	
	