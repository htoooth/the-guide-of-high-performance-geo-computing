#include "gt_geometry.h"
#include "gt_spatialindex.h"
#include "gt_datasource.h"
#include  "gt_datadriver.h"
#include "gt_layer.h"
#include "gt_spatialreference.h"
#include "mpi.h"
using namespace std;
int main( int nArgc, char *papszArgv[] )

{
	char *pszSrcFilename=NULL;
	char *layername = NULL;
	const char  *pszOutputSRSDef = NULL;
	OGRSpatialReference *poOutputSRS = new OGRSpatialReference();
	gts::GTGDOSMySQLDataSource *pSrc= new GTGDOSMySQLDataSource();
	char* oHost=NULL,*oPassword=NULL,*oUser=NULL,*oDB=NULL, **papszTableNames=NULL;
	int nPort=0,j=0;
	/*papszArgv[0]="hpgc_defineprj";
	papszArgv[1]="-l";
	papszArgv[2]="new_point6";
	papszArgv[3]="-a_srs";
	papszArgv[4]="EPSG:4326";
	papszArgv[5]="MYSQL:testing_mysql2,user=root,password=123456,host=localhost,port=3306";
	nArgc=6;*/

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
				printf("Get layer name failed");
				MPI_Finalize();
				return 0 ;
			}	

		}
		else if ( EQUAL(papszArgv[iArg], "-a_srs") )
		{
			pszOutputSRSDef = papszArgv[++iArg];
			poOutputSRS->SetFromUserInput( pszOutputSRSDef );
			if (poOutputSRS==NULL)
			{
				printf("Get srs failed");
				MPI_Finalize();
				return 0;
			}			
		}
		else if( pszSrcFilename==NULL)
		{
			pszSrcFilename = papszArgv[iArg];
			if (pszSrcFilename==NULL)
			{
				printf("Get src failed");
				MPI_Finalize();
				return 0;
			}						
		}
	}
	//****************************//
	//****Create parallel mpi*****//	
	//****************************//  
	int mpi_rank, mpi_size;	
	MPI_Init(&nArgc, &papszArgv);
	MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);	
	if(mpi_rank==0)
	{
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
			MPI_Finalize();
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
	//printf("user=%s,password=%s,host=%s,port=%d,oDB=%s,layername=%s\n",oUser,oPassword,oHost,nPort,oDB,layername);
	//****************************//
	//*******打开数据源*********//	
	//****************************//
		bool isOpen = pSrc->openDataSource("Mysql", oUser, oPassword, oHost,nPort,oDB);
		if (!isOpen)//判断是否打开数据源数据库
		{
			printf( "open datasource failed..\n");
			GTGDOSMySQLDataSource::destroyDataSource(pSrc);
			MPI_Finalize();
			return 1;
		}	
		GTFeatureLayer *pSrcLyr = pSrc->getFeatureLayerByName(layername, true);//从数据库中通过图层名取出图层，名称为point,false=update
		if (!pSrcLyr)
		{
			printf( "open layer failed..");
			GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
			MPI_Finalize();
			return 1;
		}
	
		//****************************//
		//*******获取投影类型*********//	
		//****************************//				
		GTSpatialReference *posrsputSRS =NULL;
		//posrsputSRS=pSrcLyr->getLayerSpatialRefPtr();	
		if(posrsputSRS != NULL)
			printf("The data had projection.\n");
		else		
		{					
			char **srsinfo = NULL;
			srsinfo=(char**)malloc(1000*sizeof(char));
			 for (int i = 0;i< 1000 ;i++ )
			{
				*srsinfo = (char*) malloc(sizeof(char)*1000);
			}
			poOutputSRS->exportToWkt(srsinfo);			
			pSrc->defineLayerSpatialReference(layername,*srsinfo);		
			printf("Define projection success.\n");
		}	
		GTFeatureLayer::destroyFeatureLayer(pSrcLyr);
		pSrc->closeDataSource();
		GTGDOSMySQLDataSource::destroyDataSource(pSrc);
	}
	MPI_Finalize();
	return 0;
}

