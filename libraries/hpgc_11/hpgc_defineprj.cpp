// rprjdefin.cpp : 定义控制台应用程序的入口点。
#include "stdafx.h"
#include "cpl_string.h"
#include "ogr_spatialref.h"
#include "ogr_api.h"
#include "mpi.h"
#include "gdal_priv.h"


char *SanitizeSRS( const char *pszUserInput )

{
    OGRSpatialReferenceH hSRS;
    char *pszResult = NULL;

    CPLErrorReset();
    
    hSRS = OSRNewSpatialReference( NULL );
    if( OSRSetFromUserInput( hSRS, pszUserInput ) == OGRERR_NONE )
        OSRExportToWkt( hSRS, &pszResult );
    else
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "Translating source or target SRS failed:\n%s",
                  pszUserInput );
        exit( 0 );
    }
    
    OSRDestroySpatialReference( hSRS );

    return pszResult;
}

int main( int argc, char ** argv )
{
	
	int procnum, numprocs;
	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &procnum);
	MPI_Comm_size(MPI_COMM_WORLD, &numprocs);

	if(procnum == 0)
	{
		char *pszTgtFile = NULL;
		char *pszSRS;
		pszSRS = (char*) malloc(sizeof(char)*1000);
		int i;

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

	/* -------------------------------------------------------------------- */
	/*      Register standard GDAL drivers, and process generic GDAL        */
	/*      command options.                                                */
	/* -------------------------------------------------------------------- */
		GDALAllRegister();
		argc = GDALGeneralCmdLineProcessor( argc, &argv, 0 );
		if( argc < 1 )
			exit(0);

	/* -------------------------------------------------------------------- */
	/*      Parse arguments.                                                */
	/* -------------------------------------------------------------------- */
		for( i = 1; i < argc; i++ )
		{
			if( EQUAL(argv[i], "--utility_version") )
			{
				printf("%s was compiled against GDAL %s and is running against GDAL %s\n",
					   argv[0], GDAL_RELEASE_NAME, GDALVersionInfo("RELEASE_NAME"));
				return 0;
			}
			else if( EQUAL(argv[i],"-t_srs") && i < argc-1 )
			{
				pszSRS = SanitizeSRS(argv[++i]);
			}
			else if( argv[i][0] == '-' )
			{
				printf("Wrong Parameters!\n");
				exit(0);
			}
			else
			{
				pszTgtFile = argv[i];
			}
		}
		
		GDALDataset *poTgtDS=NULL;
		poTgtDS = (GDALDataset *) GDALOpen( pszTgtFile, GA_Update );
		if( poTgtDS == NULL )
		{
			printf("Cannot open %s \n",pszTgtFile);
			exit(0);
		}

		poTgtDS->SetProjection(pszSRS);	

		
		GDALFlushCache( hDstDS );
		GDALClose(poTgtDS);
		CPLFree(pszTgtFile);
		CPLFree(pszSRS);
	}

	MPI_Finalize();

	return 0;
}

