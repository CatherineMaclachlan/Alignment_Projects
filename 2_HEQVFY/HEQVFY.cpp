//
// List missing HEQ files
//
// Usage:
// > HEQVFY layer0_48_grn_rigid_montage.xml HEQ -zmin=2 -zmax=5
//
// The input xml file can either have native or tag-modified
// file_paths. In either case, all we do here is call DskExists
// on the target images, we do not verify the state of the xml.
//

#include	"Cmdline.h"
#include	"CRegexID.h"
#include	"Disk.h"
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

private:
	// re_id used to extract tile id from image name.
	// "/N" used for EM projects, "_N_" for APIG images,
	// "_Nex.mrc" typical for Leginon files.
	CRegexID	re_id;

public:
	char	dtag[32],
			utag[32];
	char	*infile,
			*tag;
	int		zmin, zmax,
			ltag;

public:
	CArgs_heq()
	{
		infile	= NULL;
		tag		= NULL;
		zmin	= 0;
		zmax	= 32768;
	};

	void SetCmdLine( int argc, char* argv[] );

	int IDFromPatch( TiXmlElement* p );
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

	flog = FileOpenOrDie( "HEQVFY.log", "w" );

// parse command line args

	char	*pat;

	re_id.Set( "_N_" );

	if( argc < 5 ) {
usage:
		printf( "Usage: HEQVFY <xml-file> <tag> -zmin=i -zmax=j.\n" );
		exit( 42 );
	}

	for( int i = 1; i < argc; ++i ) {

		// echo to log
		fprintf( flog, "%s ", argv[i] );

		if( argv[i][0] != '-' ) {

			if( !infile )
				infile = argv[i];
			else
				tag = argv[i];
		}
		else if( GetArg( &zmin, "-zmin=%d", argv[i] ) )
			;
		else if( GetArg( &zmax, "-zmax=%d", argv[i] ) )
			;
		else if( GetArgStr( pat, "-p=", argv[i] ) )
			re_id.Set( pat );
		else {
			printf( "Did not understand option '%s'.\n", argv[i] );
			exit( 42 );
		}
	}

	if( tag ) {

		ltag = sprintf( dtag, ".%s", tag );

		utag[0] = '_';
		strcpy( utag + 1, tag );
	}
	else
		goto usage;

	fprintf( flog, "\n" );

	re_id.Compile( flog );

// header

	fprintf( flog, "\n\nMissing HEQ---\nZ\tID\n" );
	fflush( flog );
}

/* -------------------------------------------------------------- */
/* IDFromPatch -------------------------------------------------- */
/* -------------------------------------------------------------- */

int CArgs_heq::IDFromPatch( TiXmlElement* p )
{
	const char	*name = p->Attribute( "title" );
	int			id;

	if( !re_id.Decode( id, name ) ) {
		fprintf( flog, "No tile-id found in '%s'.\n", name );
		exit( 42 );
	}

	return id;
}

/* --------------------------------------------------------------- */
/* OutName ------------------------------------------------------- */
/* --------------------------------------------------------------- */

// If native image path has form par/dir/name.tif,
// and user supplied tag is HEQ, the new path is
// par/dir_HEQ/name.HEQ.tif.
//
// To check existence of HEQ object, it is enough to
// look for '.HEQ' piece.
//
static char *OutName( char *buf, TiXmlElement* p )
{
	const char	*n = p->Attribute( "file_path" );
	char		*s = strrchr( n, '/' );

	if( strstr( s + 1, gArgs.dtag ) ) {

		// xml name in proper form

		strcpy( buf, n );
	}
	else {

		// compose prospective name

		char	*dot = strrchr( s, '.' );

		sprintf( buf,
			"%.*s%s"	// path excl / + _HEQ
			"%.*s"		// / + name excl .tif
			"%s.tif",	// .HEQ.tif
			s - n, n, gArgs.utag,
			dot - s, s,
			gArgs.dtag );
	}

	return buf;
}

/* --------------------------------------------------------------- */
/* ListMissingTiles ---------------------------------------------- */
/* --------------------------------------------------------------- */

static void ListMissingTiles( TiXmlElement* layer, int z )
{
	TiXmlElement*	p = layer->FirstChildElement( "t2_patch" );

	for( ; p; p = p->NextSiblingElement() ) {

		char	buf[2048];
		int		id = gArgs.IDFromPatch( p );

		if( !DskExists( OutName( buf, p ) ) )
			fprintf( flog, "%d\t%d\n", z, id );
	}
}

/* --------------------------------------------------------------- */
/* ParseTrakEM2 -------------------------------------------------- */
/* --------------------------------------------------------------- */

static void ParseTrakEM2()
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

		/* ----------------- */
		/* Layer-level stuff */
		/* ----------------- */

		int	z = atoi( layer->Attribute( "z" ) );

		if( z > gArgs.zmax )
			break;

		if( z < gArgs.zmin )
			continue;

		ListMissingTiles( layer, z );
	}
}

/* --------------------------------------------------------------- */
/* main ---------------------------------------------------------- */
/* --------------------------------------------------------------- */

int main( int argc, char* argv[] )
{
/* ------------------ */
/* Parse command line */
/* ------------------ */

	gArgs.SetCmdLine( argc, argv );

/* ---------------- */
/* Read source file */
/* ---------------- */

	ParseTrakEM2();

/* ---- */
/* Done */
/* ---- */

	fprintf( flog, "\n" );
	fclose( flog );

	return 0;
}


