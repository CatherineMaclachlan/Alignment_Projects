//
// List missing Z-layers
//
//

#include	"Cmdline.h"
#include	"File.h"
#include	"TrakEM2_UTL.h"


/* --------------------------------------------------------------- */
/* Macros -------------------------------------------------------- */
/* --------------------------------------------------------------- */

/* --------------------------------------------------------------- */
/* Types --------------------------------------------------------- */
/* --------------------------------------------------------------- */

/* --------------------------------------------------------------- */
/* CArgs_heq ----------------------------------------------------- */
/* --------------------------------------------------------------- */

class CArgs_heq {

public:
    char	*infile;
    int		zmin, zmax;

public:
    CArgs_heq()
    {
        infile	= NULL;
        zmin	= 0;
        zmax	= 32768;
    };

    void SetCmdLine( int argc, char* argv[] );
};

/* --------------------------------------------------------------- */
/* Statics ------------------------------------------------------- */
/* --------------------------------------------------------------- */

static CArgs_heq	gArgs;
static FILE*		flog = NULL;






/* --------------------------------------------------------------- */
/* SetCmdLine ---------------------------------------------------- */
/* --------------------------------------------------------------- */

void CArgs_heq::SetCmdLine( int argc, char* argv[] )
{
// start log

    flog = FileOpenOrDie( "XMLMissingZ.log", "w" );

// parse command line args

    if( argc < 2 ) {
        printf( "Usage: XMLMissingZ <xml-file> -zmin=i -zmax=j.\n" );
        exit( 42 );
    }

    for( int i = 1; i < argc; ++i ) {

        // echo to log
        fprintf( flog, "%s ", argv[i] );

        if( argv[i][0] != '-' ) {

            if( !infile )
                infile = argv[i];
        }
        else if( GetArg( &zmin, "-zmin=%d", argv[i] ) )
            ;
        else if( GetArg( &zmax, "-zmax=%d", argv[i] ) )
            ;
        else {
            printf( "Did not understand option '%s'.\n", argv[i] );
            exit( 42 );
        }
    }

// header

    fprintf( flog, "\n\n" );
    fflush( flog );
}

/* --------------------------------------------------------------- */
/* ParseTrakEM2 -------------------------------------------------- */
/* --------------------------------------------------------------- */

static void ParseTrakEM2( vector<char> &bz )
{
/* ---- */
/* Open */
/* ---- */

    XML_TKEM		xml( gArgs.infile, flog );
    TiXmlElement*	layer	= xml.GetFirstLayer();

/* -------------- */
/* For each layer */
/* -------------- */

    for( ; layer; layer = layer->NextSiblingElement() ) {

        int	z = atoi( layer->Attribute( "z" ) );

        if( z > gArgs.zmax )
            break;

        if( z < gArgs.zmin )
            continue;

        bz[z] = 1;
    }
}

/* --------------------------------------------------------------- */
/* Report -------------------------------------------------------- */
/* --------------------------------------------------------------- */

static void Report( const vector<char> &bz )
{
    int	hi = gArgs.zmax;

    for( ; hi >= gArgs.zmin; --hi ) {

        if( bz[hi] )
            break;
    }

    fprintf( flog, "Missing Z (highest=%d)---\n", hi );

    for( int i = gArgs.zmin; i < hi; ++i ) {

        if( !bz[i] )
            fprintf( flog, "%d\n", i );
    }
}

/* --------------------------------------------------------------- */
/* main ---------------------------------------------------------- */
/* --------------------------------------------------------------- */

int main( int argc, char* argv[] )
{
    vector<char>	bz( gArgs.zmax + 1, 0 );

/* ------------------ */
/* Parse command line */
/* ------------------ */

    gArgs.SetCmdLine( argc, argv );

/* ---------------- */
/* Read source file */
/* ---------------- */

    ParseTrakEM2( bz );

/* ----------- */
/* Report gaps */
/* ----------- */

    Report( bz );

/* ---- */
/* Done */
/* ---- */

    fprintf( flog, "\n" );
    fclose( flog );

    return 0;
}


