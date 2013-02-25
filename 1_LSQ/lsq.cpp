

// Based on Lou's 11/23/2010 copy of lsq.cpp


#include	"lsq_ReadPts.h"
#include	"lsq_Rigid.h"
#include	"lsq_Affine.h"
#include	"lsq_Hmgphy.h"

#include	"Cmdline.h"
#include	"CRegexID.h"
#include	"Disk.h"
#include	"File.h"
#include	"TrakEM2_UTL.h"
#include	"PipeFiles.h"
#include	"CAffineLens.h"
#include	"LinEqu.h"
#include	"ImageIO.h"
#include	"Maths.h"

#include	<math.h>


/* --------------------------------------------------------------- */
/* CArgs_lsq ----------------------------------------------------- */
/* --------------------------------------------------------------- */

class CArgs_lsq {

private:
	// re_id used to extract tile id from image name.
	// "/N" used for EM projects, "_N_" for APIG images,
	// "_Nex.mrc" typical for Leginon files.
	CRegexID	re_id;

public:
	// xml_type values: these are ImagePlus codes:
	// AUTO			= -1
	// GRAY8		= 0
	// GRAY16		= 1
	// GRAY32		= 2
	// COLOR_256	= 3
	// COLOR_RGB	= 4
	//
	vector<MZID>	include_only;	// if given, include only these
	vector<double>	lrbt;			// if not empty, forced bbox
	double	same_strength,
			square_strength,
			scale_strength,
			tfm_tol,			// transform uniformity tol
			thresh,				// outlier if worse than this
			trim,				// trim this off XML images
			degcw;				// rotate clockwise degrees
	char	*pts_file,
			*dir_file,
			*tfm_file;
	int		model,				// model type {R,A,H}
			unite_layer,
			ref_layer,
			max_pass,
			xml_type,
			xml_min,
			xml_max,
			viserr;				// 0, or, error scale
	bool	strings,
			lens,
			make_layer_square,
			use_all;			// align even if #pts < 3/tile

public:
	CArgs_lsq()
	{
		same_strength		= 1.0;
		square_strength		= 0.1;
		scale_strength		= 0.1;
		tfm_tol				= -1.0;
		thresh				= 700.0;
		trim				= 0.0;
		degcw				= 0.0;
		pts_file			= NULL;
		dir_file			= NULL;
		tfm_file			= NULL;
		model				= 'A';
		unite_layer			= -1;
		ref_layer			= -1;
		max_pass			= 1;
		xml_type			= 0;
		xml_min				= 0;
		xml_max				= 0;
		viserr				= 0;
		strings				= false;
		lens				= false;
		make_layer_square	= false;
		use_all				= false;
	};

	void SetCmdLine( int argc, char* argv[] );

	int TileIDFromName( const char *name );
};

/* --------------------------------------------------------------- */
/* EVL ----------------------------------------------------------- */
/* --------------------------------------------------------------- */

// Error evaluation

class EVL {

private:
	typedef struct Error {

		double	amt;
		int		idx;	// which constraint

		Error()	{};
		Error( double err, int id )
			{amt = err; idx = id;};

		bool operator < (const Error &rhs) const
			{return amt < rhs.amt;};
	} Error;

	typedef struct SecErr {

		Point	loc;	// global error location
		double	err;	// worst err
		int		idx;	// which constraint

		SecErr()
			{err = 0; idx = -1;};

		SecErr( const Point	&p1,
				const Point	&p2,
				double		e,
				int			id )
			{
				loc = Point( (p1.x+p2.x)/2, (p1.y+p2.y)/2 );
				err = e;
				idx = id;
			};
	} SecErr;

	typedef struct VisErr {

		double	L, R, B, T, D;
	}  VisErr;

private:
	vector<Error>	Epnt;	// each constraint
	vector<SecErr>	Ein,	// worst in- and between-layer
					Ebt;

private:
	void Tabulate(
		const vector<zsort>		&zs,
		const vector<double>	&X );

	void Line(
		FILE	*f,
		double	xfrom,
		double	yfrom,
		double	xto,
		double	yto );

	void BoxOrCross( FILE *f, double x, double y, bool box );
	void Arrow( FILE *f, const Point &g1, const Point &g2 );

	void Print_be_and_se_files( const vector<zsort> &zs );
	void Print_errs_by_layer( const vector<zsort> &zs );

	void ViseEval1(
		vector<VisErr>			&ve,
		const vector<double>	&X );

	void ViseEval2(
		vector<VisErr>			&ve,
		const vector<double>	&X );

	void BuildVise(
		double					xmax,
		double					ymax,
		const vector<zsort>		&zs,
		const vector<double>	&X );

public:
	void Evaluate(
		double					xmax,
		double					ymax,
		const vector<zsort>		&zs,
		const vector<double>	&X );
};

/* --------------------------------------------------------------- */
/* Statics ------------------------------------------------------- */
/* --------------------------------------------------------------- */

static CArgs_lsq	gArgs;
static FILE			*FOUT;			// outfile: 'simple'
static MDL			*M		= NULL;
static int			gNTr	= 0;	// Set by Set_itr_set_used






/* --------------------------------------------------------------- */
/* SetCmdLine ---------------------------------------------------- */
/* --------------------------------------------------------------- */

void CArgs_lsq::SetCmdLine( int argc, char* argv[] )
{
// Parse command line args

	printf( "---- Read params ----\n" );

	vector<int>	vi;
	char		*pat;

	re_id.Set( "/N" );

	if( argc < 2 ) {
		printf(
		"Usage: lsq <file of points>"
		" [<file of directory-name - layer-number correspondences>]"
		" [options].\n" );
		exit( 42 );
	}

	for( int i = 1; i < argc; ++i ) {

		char	*instr;

		if( argv[i][0] != '-' ) {

			if( !pts_file )
				pts_file = argv[i];
			else
				dir_file = argv[i];
		}
		else if( GetArgList( vi, "-only=", argv[i] ) ) {

			int ni = vi.size();

			if( ni & 1 )
				--ni;

			for( int i = 0; i < ni; i += 2 ) {

				include_only.push_back(
					MZID( vi[i], vi[i+1] ) );

				printf( "Include only %4d %4d\n", vi[i], vi[i+1] );
			}

			printf( "<-only> option not implemented; IGNORED.\n" );
		}
		else if( GetArgList( lrbt, "-lrbt=", argv[i] ) ) {

			if( 4 != lrbt.size() ) {
				printf( "Bad format in -lrbt [%s].\n", argv[i] );
				exit( 42 );
			}
		}
		else if( GetArg( &same_strength, "-same=%lf", argv[i] ) )
			printf( "Setting same-layer strength to %f\n", same_strength );
		else if( GetArg( &square_strength, "-square=%lf", argv[i] ) )
			printf( "Setting square strength to %f\n", square_strength );
		else if( GetArg( &scale_strength, "-scale=%lf", argv[i] ) )
			printf( "Setting scale strength to %f\n", scale_strength );
		else if( GetArg( &tfm_tol, "-tformtol=%lf", argv[i] ) )
			printf( "Setting tform uniformity to %f\n", tfm_tol );
		else if( GetArg( &thresh, "-threshold=%lf", argv[i] ) )
			printf( "Setting threshold to %f\n", thresh );
		else if( GetArg( &trim, "-trim=%lf", argv[i] ) )
			printf( "Setting trim amount to %f\n", trim );
		else if( GetArg( &degcw, "-degcw=%lf", argv[i] ) )
			printf( "Setting deg-cw to %f\n", degcw );
		else if( GetArgStr( instr, "-model=", argv[i] ) ) {

			model = toupper( instr[0] );

			switch( model ) {
				case 'R':
					printf( "Setting model to rigid\n" );
				break;
				case 'A':
					printf( "Setting model to affine\n" );
				break;
				case 'H':
					printf( "Setting model to homography\n" );
				break;
				default:
					printf( "Bad model [%s].\n", argv[i] );
					exit( 42 );
				break;
			}
		}
		else if( GetArgStr( instr, "-unite=", argv[i] ) ) {

			char	buf[2048];

			sscanf( instr, "%d,%s", &unite_layer, buf );
			tfm_file = strdup( buf );

			printf( "Uniting: layer %d of '%s'.\n",
			unite_layer, tfm_file );
		}
		else if( GetArg( &ref_layer, "-refz=%d", argv[i] ) )
			printf( "Reference layer %d\n", ref_layer );
		else if( GetArg( &max_pass, "-pass=%d", argv[i] ) )
			printf( "Setting maximum passes to %d\n", max_pass );
		else if( GetArg( &xml_type, "-xmltype=%d", argv[i] ) )
			printf( "Setting xml image type to %d\n", xml_type );
		else if( GetArg( &xml_min, "-xmlmin=%d", argv[i] ) )
			printf( "Setting xml image min to %d\n", xml_min );
		else if( GetArg( &xml_max, "-xmlmax=%d", argv[i] ) )
			printf( "Setting xml image max to %d\n", xml_max );
		else if( GetArg( &viserr, "-viserr=%d", argv[i] ) )
			printf( "Setting visual error scale to %d\n", viserr );
		else if( IsArg( "-strings", argv[i] ) )
			strings = true;
		else if( IsArg( "-lens", argv[i] ) )
			lens = true;
		else if( IsArg( "-mls", argv[i] ) ) {
			make_layer_square = true;
			printf( "Making reference layer square.\n" );
		}
		else if( IsArg( "-all", argv[i] ) ) {
			use_all = true;
			printf( "Using all correspondences.\n" );
		}
		else if( GetArgStr( pat, "-p=", argv[i] ) ) {
			re_id.Set( pat );
			printf( "Setting pattern '%s'.\n", pat );
		}
		else {
			printf( "Did not understand option '%s'.\n", argv[i] );
			exit( 42 );
		}
	}

	if( strings )
		re_id.Compile( stdout );
	else
		printf( "\n" );
}

/* --------------------------------------------------------------- */
/* TileIDFromName ------------------------------------------------ */
/* --------------------------------------------------------------- */

int CArgs_lsq::TileIDFromName( const char *name )
{
	const char	*s = FileNamePtr( name );
	int			id;

	if( !re_id.Decode( id, s ) ) {
		printf( "No tile-id found in '%s'.\n", s );
		exit( 42 );
	}

	return id;
}

/* --------------------------------------------------------------- */
/* Callback IDFromName ------------------------------------------- */
/* --------------------------------------------------------------- */

static int IDFromName( const char *name )
{
	return gArgs.TileIDFromName( name );
}

/* --------------------------------------------------------------- */
/* SetPointPairs ------------------------------------------------- */
/* --------------------------------------------------------------- */

#if 0
static void SetPointPairs(
	vector<LHSCol>	&LHS,
	vector<double>	&RHS,
	double			sc )
{
	int	nc	= vAllC.size();

	for( int i = 0; i < nc; ++i ) {

		const Constraint &C = vAllC[i];

		if( !C.used || !C.inlier )
			continue;

		double	fz =
		(vRgn[C.r1].z == vRgn[C.r2].z ? gArgs.same_strength : 1);

		double	x1 = C.p1.x * fz / sc,
				y1 = C.p1.y * fz / sc,
				x2 = C.p2.x * fz / sc,
				y2 = C.p2.y * fz / sc;
		int		j  = vRgn[C.r1].itr * 6,
				k  = vRgn[C.r2].itr * 6;

		// T1(p1) - T2(p2) = 0

		double	v[6]  = {x1,  y1,   fz, -x2, -y2,  -fz};
		int		i1[6] = {j,   j+1, j+2, k,   k+1,  k+2};
		int		i2[6] = {j+3, j+4, j+5, k+3, k+4,  k+5};

		AddConstraint( LHS, RHS, 6, i1, v, 0.0 );
		AddConstraint( LHS, RHS, 6, i2, v, 0.0 );
	}
}
#endif

/* --------------------------------------------------------------- */
/* SetIdentityTForm ---------------------------------------------- */
/* --------------------------------------------------------------- */

// Explicitly set some TForm to Identity.
// @@@ Does it matter which one we use?
//
static void SetIdentityTForm(
	vector<LHSCol>	&LHS,
	vector<double>	&RHS )
{
	double	stiff	= 1.0;

	double	one	= stiff;
	int		j	= (gNTr / 2) * 6;

	AddConstraint( LHS, RHS, 1, &j, &one, one );	j++;
	AddConstraint( LHS, RHS, 1, &j, &one, 0 );		j++;
	AddConstraint( LHS, RHS, 1, &j, &one, 0 );		j++;
	AddConstraint( LHS, RHS, 1, &j, &one, 0 );		j++;
	AddConstraint( LHS, RHS, 1, &j, &one, one );	j++;
	AddConstraint( LHS, RHS, 1, &j, &one, 0 );		j++;

// Report which tile we set

	int	nr = vRgn.size();

	for( int k = 0; k < nr; ++k ) {

		if( vRgn[k].itr == gNTr / 2 ) {

			printf( "Ref region z=%d, id=%d\n",
			vRgn[k].z, vRgn[k].id );
			break;
		}
	}
}

/* --------------------------------------------------------------- */
/* SetUniteLayer ------------------------------------------------- */
/* --------------------------------------------------------------- */

// Set one layer-full of TForms to those from a previous
// solution output file gArgs.tfm_file.
//
static void SetUniteLayer(
	vector<LHSCol>	&LHS,
	vector<double>	&RHS,
	double			sc )
{
/* ------------------------------- */
/* Load TForms for requested layer */
/* ------------------------------- */

	map<MZIDR,TAffine>	M;

	LoadTAffineTbl_ThisZ( M, gArgs.unite_layer, gArgs.tfm_file );

/* ----------------------------- */
/* Set each TForm in given layer */
/* ----------------------------- */

	double	stiff	= 10.0;

	int	nr = vRgn.size();

	for( int i = 0; i < nr; ++i ) {

		const RGN&	R = vRgn[i];

		if( R.z != gArgs.unite_layer || R.itr < 0 )
			continue;

		map<MZIDR,TAffine>::iterator	it;

		it = M.find( MZIDR( R.z, R.id, R.rgn ) );

		if( it == M.end() )
			continue;

		double	one	= stiff,
				*t	= it->second.t;
		int		j	= R.itr * 6;

		AddConstraint( LHS, RHS, 1, &j, &one, one*t[0] );		j++;
		AddConstraint( LHS, RHS, 1, &j, &one, one*t[1] );		j++;
		AddConstraint( LHS, RHS, 1, &j, &one, one*t[2] / sc );	j++;
		AddConstraint( LHS, RHS, 1, &j, &one, one*t[3] );		j++;
		AddConstraint( LHS, RHS, 1, &j, &one, one*t[4] );		j++;
		AddConstraint( LHS, RHS, 1, &j, &one, one*t[5] / sc );	j++;
	}
}

/* --------------------------------------------------------------- */
/* PrintMagnitude ------------------------------------------------ */
/* --------------------------------------------------------------- */

static void PrintMagnitude( const vector<double> &X, int nvars )
{
	int	k = X.size() - nvars;

	if( k >= 0 ) {

		double	mag	= sqrt( X[k]*X[k] + X[k+1]*X[k+1] );

		printf( "Final magnitude is %g\n", mag );
	}

	fflush( stdout );
}

/* --------------------------------------------------------------- */
/* SolveWithSquareness ------------------------------------------- */
/* --------------------------------------------------------------- */

static void SolveWithSquareness(
	vector<double>	&X,
	vector<LHSCol>	&LHS,
	vector<double>	&RHS )
{
/* -------------------------- */
/* Add squareness constraints */
/* -------------------------- */

	double	stiff = gArgs.square_strength;

	for( int i = 0; i < gNTr; ++i ) {

		int	j = i * 6;

		// equal cosines
		{
			double	V[2] = {stiff, -stiff};
			int		I[2] = {j, j+4};

			AddConstraint( LHS, RHS, 2, I, V, 0.0 );
		}

		// opposite sines
		{
			double	V[2] = {stiff, stiff};
			int		I[2] = {j+1, j+3};

			AddConstraint( LHS, RHS, 2, I, V, 0.0 );
		}
	}

/* ----------------- */
/* 1st pass solution */
/* ----------------- */

// We have enough info for first estimate of the global
// transforms. We will need these to formulate further
// constraints on the global shape and scale.

	printf( "Solve with [transform squareness].\n" );
	WriteSolveRead( X, LHS, RHS, false );
	PrintMagnitude( X, 6 );
}

/* --------------------------------------------------------------- */
/* SolveWithMontageSqr ------------------------------------------- */
/* --------------------------------------------------------------- */

// Add some constraints so the left edge of the array
// needs to be the same length as the right edge, and
// repeat for top and bottom. Of course, this assumes
// a symmetric montage.
//
#if 1
static void SolveWithMontageSqr(
	vector<double>	&X,
	vector<LHSCol>	&LHS,
	vector<double>	&RHS )
{
	const int	MAXINT = 0x7FFFFFFF;

	int	nr = vRgn.size();

/* ------------ */
/* Which layer? */
/* ------------ */

	if( gArgs.ref_layer < 0 ) {

		gArgs.ref_layer = MAXINT;

		for( int i = 0; i < nr; ++i ) {

			if( vRgn[i].itr >= 0 ) {

				gArgs.ref_layer =
					min( gArgs.ref_layer, vRgn[i].z );
			}
		}

		printf(
		"\nNo reference layer specified,"
		" using lowest layer %d\n", gArgs.ref_layer );
	}

/* ---------------------------------------- */
/* Search for greatest outward translations */
/* ---------------------------------------- */

	double	vNW = -MAXINT,
			vNE = -MAXINT,
			vSE = -MAXINT,
			vSW = -MAXINT;
	int		iNW, iNE, iSE, iSW,
			jNW, jNE, jSE, jSW;

	for( int i = 0; i < nr; ++i ) {

		if( vRgn[i].z != gArgs.ref_layer )
			continue;

		if( vRgn[i].itr < 0 )
			continue;

		double	v;
		int		j = vRgn[i].itr * 6;

		if( (v =  X[j+2] + X[j+5]) > vNE )
			{iNE = i; jNE = j; vNE = v;}

		if( (v =  X[j+2] - X[j+5]) > vSE )
			{iSE = i; jSE = j; vSE = v;}

		if( (v = -X[j+2] + X[j+5]) > vNW )
			{iNW = i; jNW = j; vNW = v;}

		if( (v = -X[j+2] - X[j+5]) > vSW )
			{iSW = i; jSW = j; vSW = v;}
	}

	printf(
	"Corner tiles are:"
	" se %d (%f %f),"
	" ne %d (%f %f),"
	" nw %d (%f %f),"
	" sw %d (%f %f).\n",
	vRgn[iSE].id, X[jSE+2], X[jSE+5],
	vRgn[iNE].id, X[jNE+2], X[jNE+5],
	vRgn[iNW].id, X[jNW+2], X[jNW+5],
	vRgn[iSW].id, X[jSW+2], X[jSW+5] );

/* ------------------------------------------- */
/* Use these corner tiles to impose squareness */
/* ------------------------------------------- */

	double	stiff = gArgs.square_strength;

// Top = bottom (DX = DX)

	{
		double	V[4] = {stiff, -stiff, -stiff, stiff};
		int		I[4] = {jSE+2,  jSW+2,  jNE+2, jNW+2};

		AddConstraint( LHS, RHS, 4, I, V, 0.0 );
	}

// Left = right (DY = DY)

	{
		double	V[4] = {stiff, -stiff, -stiff, stiff};
		int		I[4] = {jSE+5,  jSW+5,  jNE+5, jNW+5};

		AddConstraint( LHS, RHS, 4, I, V, 0.0 );
	}

/* --------------- */
/* Update solution */
/* --------------- */

	printf( "Solve with [montage squareness].\n" );
	WriteSolveRead( X, LHS, RHS, false );
	PrintMagnitude( X, 6 );
}
#endif

#if 0
// Experiment to simply hardcode which tiles to use as corners.
// In practice, though, looks like this constraint causes montage
// to buckle if there really is a natural warp like a banana shape,
// so not recommended.
//
static void SolveWithMontageSqr(
	vector<double>	&X,
	vector<LHSCol>	&LHS,
	vector<double>	&RHS )
{
	int	nr = vRgn.size();

/* ------------------------ */
/* Assign hand-picked tiles */
/* ------------------------ */

	int	jNW, jNE, jSE, jSW, nass = 0;

	for( int i = 0; i < nr && nass < 4; ++i ) {

		if( vRgn[i].itr < 0 )
			continue;

		int id = vRgn[i].id;

		if( id == 19001000 ) {
			jNW = i;
			++nass;
		}
		else if( id == 19069000 ) {
			jNE = i;
			++nass;
		}
		else if( id == 19001149 ) {
			jSW = i;
			++nass;
		}
		else if( id == 19069149 ) {
			jSE = i;
			++nass;
		}
	}

	if( nass != 4 ) {
		printf( "***   ***   *** Missing squareness corner.\n" );
		return;
	}

/* ------------------------------------------- */
/* Use these corner tiles to impose squareness */
/* ------------------------------------------- */

	double	stiff = gArgs.square_strength;

// Top = bottom (DX = DX)

	{
		double	V[4] = {stiff, -stiff, -stiff, stiff};
		int		I[4] = {jSE+2,  jSW+2,  jNE+2, jNW+2};

		AddConstraint( LHS, RHS, 4, I, V, 0.0 );
	}

// Left = right (DY = DY)

	{
		double	V[4] = {stiff, -stiff, -stiff, stiff};
		int		I[4] = {jSE+5,  jSW+5,  jNE+5, jNW+5};

		AddConstraint( LHS, RHS, 4, I, V, 0.0 );
	}

/* --------------- */
/* Update solution */
/* --------------- */

	printf( "Solve with [montage squareness].\n" );
	WriteSolveRead( X, LHS, RHS, false );
	PrintMagnitude( X, 6 );
}
#endif

/* --------------------------------------------------------------- */
/* SolveWithUnitMag ---------------------------------------------- */
/* --------------------------------------------------------------- */

// Effectively, we want to constrain the cosines and sines
// so that c^2 + s^2 = 1. We can't make constraints that are
// non-linear in the variables X[], but we can construct an
// approximation using the {c,s = X[]} of the previous fit:
// c*x + s*y = 1. To reduce sensitivity to the sizes of the
// previous fit c,s, we normalize them by m = sqrt(c^2 + s^2).
//
static void SolveWithUnitMag(
	vector<double>	&X,
	vector<LHSCol>	&LHS,
	vector<double>	&RHS )
{
	double	stiff = gArgs.scale_strength;

	for( int i = 0; i < gNTr; ++i ) {

		int		j = i * 6;
		double	c = X[j];
		double	s = X[j+3];
		double	m = sqrt( c*c + s*s );

		// c*x/m + s*y/m = 1

		double	V[2] = {c * stiff, s * stiff};
		int		I[2] = {j, j+3};

		AddConstraint( LHS, RHS, 2, I, V, m * stiff );
	}

	printf( "Solve with [unit magnitude].\n" );
	WriteSolveRead( X, LHS, RHS, false );
	printf( "\t\t\t\t" );
	PrintMagnitude( X, 6 );
}

/* --------------------------------------------------------------- */
/* SolveSystem --------------------------------------------------- */
/* --------------------------------------------------------------- */

// Build and solve system of linear equations.
//
// Note:
// All translational variables are scaled down by 'scale' so they
// are sized similarly to the sine/cosine variables. This is only
// to stabilize solver algorithm. We undo the scaling on exit.
//
static void SolveSystem( vector<double> &X )
{
	double	scale	= 2 * max( gW, gH );
	int		nvars	= gNTr * 6;

	printf( "%d unknowns; %d constraints.\n", nvars, vAllC.size() );

	vector<double> RHS( nvars, 0.0 );
	vector<LHSCol> LHS( nvars );

	X.resize( nvars );

/* ------------------ */
/* Get rough solution */
/* ------------------ */

//	SetPointPairs( LHS, RHS, scale );
	M->SetPointPairs( LHS, RHS, scale, gArgs.same_strength );

	if( gArgs.unite_layer < 0 )
		SetIdentityTForm( LHS, RHS );
	else
		SetUniteLayer( LHS, RHS, scale );

	SolveWithSquareness( X, LHS, RHS );

/* ----------------------------------------- */
/* Use solution to add torsional constraints */
/* ----------------------------------------- */

	if( gArgs.make_layer_square )
		SolveWithMontageSqr( X, LHS, RHS );

	SolveWithUnitMag( X, LHS, RHS );

/* --------------------------- */
/* Rescale translational terms */
/* --------------------------- */

	int	nr	= vRgn.size();

	for( int i = 0; i < nr; ++i ) {

		if( vRgn[i].itr >= 0 ) {

			int j = vRgn[i].itr * 6;

			X[j+2] *= scale;
			X[j+5] *= scale;
		}
	}
}

/* --------------------------------------------------------------- */
/* SolveSystemRigid ---------------------------------------------- */
/* --------------------------------------------------------------- */

// Like CanAlign, solve for 4-var transforms Y, but return 6-var
// transforms X to caller.
//
// Although this version functions correctly, cross-layer matching
// is not as good as the full affine (as expected). This version is
// retained only as an example.
//
static void SolveSystemRigid( vector<double> &X )
{
	int	nvars	= gNTr * 4,
		nc		= vAllC.size(),
		nr		= vRgn.size();

	printf( "%d unknowns; %d constraints.\n", nvars, nc );

	vector<double> Y( nvars );
	vector<double> RHS( nvars, 0.0 );
	vector<LHSCol> LHS( nvars );

/* ------------------------- */
/* Add point-pair transforms */
/* ------------------------- */

// All translational variables are scaled down by 'scale' so they
// are sized similarly to the sine/cosine variables. This is only
// to stabilize solver algorithm. We undo the scaling on exit.

	double	scale = 2 * max( gW, gH );

	for( int i = 0; i < nc; ++i ) {

		const Constraint &C = vAllC[i];

		if( !C.used || !C.inlier )
			continue;

		double	x1 = C.p1.x / scale,
				y1 = C.p1.y / scale,
				x2 = C.p2.x / scale,
				y2 = C.p2.y / scale;
		int		j  = vRgn[C.r1].itr * 4,
				k  = vRgn[C.r2].itr * 4;

		// T1(p1) - T2(p2) = 0

		double	v[6]  = {x1,  -y1, 1.0, -x2,  y2, -1.0};
		int		i1[6] = {j,   j+1, j+2, k,   k+1,  k+2};
		int		i2[6] = {j+1,   j, j+3, k+1,   k,  k+3};

		AddConstraint( LHS, RHS, 6, i1, v, 0.0 );

		v[1] = y1;
		v[4] = -y2;

		AddConstraint( LHS, RHS, 6, i2, v, 0.0 );
	}

/* ------------------------------- */
/* Make one the identity transform */
/* ------------------------------- */

// Explicitly set each element of some transform.
// @@@ Does it matter which one we use?

	double	stiff	= 1.0;

	double	one	= stiff;
	int		j	= (gNTr / 2) * 4;

	AddConstraint( LHS, RHS, 1, &j, &one, one );	j++;
	AddConstraint( LHS, RHS, 1, &j, &one, 0 );		j++;
	AddConstraint( LHS, RHS, 1, &j, &one, 0 );		j++;
	AddConstraint( LHS, RHS, 1, &j, &one, 0 );		j++;

/* ----------------- */
/* 1st pass solution */
/* ----------------- */

// We have enough info for first estimate of the global
// transforms. We will need these to formulate further
// constraints on the global shape and scale.

	printf( "Solve with [rigid only].\n" );
	WriteSolveRead( Y, LHS, RHS, false );
	PrintMagnitude( Y, 4 );

/* ------------------- */
/* Make montage square */
/* ------------------- */

// Add some constraints so the left edge of the array
// needs to be the same length as the right edge, and
// repeat for top and bottom.

	const int	MAXINT = 0x7FFFFFFF;

	if( gArgs.make_layer_square ) {

		/* ------------ */
		/* Which layer? */
		/* ------------ */

		if( gArgs.ref_layer < 0 ) {

			gArgs.ref_layer = MAXINT;

			for( int i = 0; i < nr; ++i ) {

				if( vRgn[i].itr >= 0 ) {

					gArgs.ref_layer =
						min( gArgs.ref_layer, vRgn[i].z );
				}
			}

			printf(
			"\nNo reference layer specified,"
			" using lowest layer %d\n", gArgs.ref_layer );
		}

		/* ---------------------------------------- */
		/* Search for greatest outward translations */
		/* ---------------------------------------- */

		double	vNW = -MAXINT,
				vNE = -MAXINT,
				vSE = -MAXINT,
				vSW = -MAXINT;
		int		iNW, iNE, iSE, iSW,
				jNW, jNE, jSE, jSW;

		for( int i = 0; i < nr; ++i ) {

			if( vRgn[i].z != gArgs.ref_layer )
				continue;

			if( vRgn[i].itr < 0 )
				continue;

			double	v;
			int		j = vRgn[i].itr * 4;

			if( (v =  Y[j+2] + Y[j+3]) > vNE )
				{iNE = i; jNE = j; vNE = v;}

			if( (v =  Y[j+2] - Y[j+3]) > vSE )
				{iSE = i; jSE = j; vSE = v;}

			if( (v = -Y[j+2] + Y[j+3]) > vNW )
				{iNW = i; jNW = j; vNW = v;}

			if( (v = -Y[j+2] - Y[j+3]) > vSW )
				{iSW = i; jSW = j; vSW = v;}
		}

		printf(
		"Corner tiles are:"
		" se %d (%f %f),"
		" ne %d (%f %f),"
		" nw %d (%f %f),"
		" sw %d (%f %f).\n",
		vRgn[iSE].id, Y[jSE+2], Y[jSE+3],
		vRgn[iNE].id, Y[jNE+2], Y[jNE+3],
		vRgn[iNW].id, Y[jNW+2], Y[jNW+3],
		vRgn[iSW].id, Y[jSW+2], Y[jSW+3] );

		/* ------------------------------------------- */
		/* Use these corner tiles to impose squareness */
		/* ------------------------------------------- */

		stiff = gArgs.square_strength;

		// Top = bottom (DX = DX)
		{
			double	V[4] = {stiff, -stiff, -stiff, stiff};
			int		I[4] = {jSE+2,  jSW+2,  jNE+2, jNW+2};

			AddConstraint( LHS, RHS, 4, I, V, 0.0 );
		}

		// Left = right (DY = DY)
		{
			double	V[4] = {stiff, -stiff, -stiff, stiff};
			int		I[4] = {jSE+3,  jSW+3,  jNE+3, jNW+3};

			AddConstraint( LHS, RHS, 4, I, V, 0.0 );
		}

		// Update solution

		printf( "Solve with [montage squareness].\n" );
		WriteSolveRead( Y, LHS, RHS, false );
		PrintMagnitude( Y, 4 );
	}

/* ------------------- */
/* Make unit magnitude */
/* ------------------- */

// Effectively, we want to constrain the cosines and sines
// so that c^2 + s^2 = 1. We can't make constraints that are
// non-linear in the variables Y[], but we can construct an
// approximation using the {c,s = Y[]} of the previous fit:
// c*x + s*y = 1. To reduce sensitivity to the sizes of the
// previous fit c,s, we normalize them by m = sqrt(c^2 + s^2).

	stiff = gArgs.scale_strength;

	for( int i = 0; i < gNTr; ++i ) {

		int		j = i * 4;
		double	c = Y[j];
		double	s = Y[j+1];
		double	m = sqrt( c*c + s*s );

		// c*x/m + s*y/m = 1

		double	V[2] = {c * stiff, s * stiff};
		int		I[2] = {j, j+1};

		AddConstraint( LHS, RHS, 2, I, V, m * stiff );
	}

	printf( "Solve with [unit magnitude].\n" );
	WriteSolveRead( Y, LHS, RHS, false );
	printf( "\t\t\t\t" );
	PrintMagnitude( Y, 4 );

/* --------------------------- */
/* Rescale translational terms */
/* --------------------------- */

	for( int i = 0; i < nr; ++i ) {

		if( vRgn[i].itr >= 0 ) {

			int j = vRgn[i].itr * 4;

			Y[j+2] *= scale;
			Y[j+3] *= scale;
		}
	}

/* ---------------------------- */
/* Return expanded 6-var copies */
/* ---------------------------- */

	X.resize( gNTr * 6 );

	for( int i = 0; i < gNTr; ++i ) {

		double	*dst = &X[i * 6],
				*src = &Y[i * 4];

		dst[0] = src[0];
		dst[4] = src[0];
		dst[1] = -src[1];
		dst[3] = src[1];
		dst[2] = src[2];
		dst[5] = src[3];
	}
}

/* --------------------------------------------------------------- */
/* OutlierTform classes ------------------------------------------ */
/* --------------------------------------------------------------- */

// Some set of values that are derived from TForm

class Tfmval {

private:
	double	t0, t1, t3, t4;

public:
	Tfmval()
		{};
	Tfmval( const double *X )
		{t0=X[0];t1=X[1];t3=X[3];t4=X[4];};

	bool IsOutlier(
		const Tfmval	&ref,
		double			tol,
		int				z,
		int				id,
		FILE			*f );

	static void PrintHdr( FILE *f )
		{fprintf( f, "Lyr\tTil\tDT0\tDT1\tDT3\tDT4\n" );};
};

bool Tfmval::IsOutlier(
	const Tfmval	&ref,
	double			tol,
	int				z,
	int				id,
	FILE			*f )
{
	double dt0 = fabs( t0 - ref.t0 );
	double dt1 = fabs( t1 - ref.t1 );
	double dt3 = fabs( t3 - ref.t3 );
	double dt4 = fabs( t4 - ref.t4 );

	if( dt0 > tol || dt1 > tol || dt3 > tol || dt4 > tol ) {

		fprintf( f, "%d\t%d\t%f\t%f\t%f\t%f\n",
		z, id, dt0, dt1, dt3, dt4 );

		return true;
	}

	return false;
}


// Implement layer-wise calc for one or more values
// derived from TForm elements

class CLayerTfmvalCalc {

private:
	vector<double>	t0, t1, t3, t4;

public:
	void Add( const double *X );

	int Size()
		{return t0.size();};

	Tfmval LayerVals();

	void Reset()
		{t0.clear();t1.clear();t3.clear();t4.clear();};
};

void CLayerTfmvalCalc::Add( const double *X )
{
	t0.push_back( X[0] );
	t1.push_back( X[1] );
	t3.push_back( X[3] );
	t4.push_back( X[4] );
}

Tfmval CLayerTfmvalCalc::LayerVals()
{
	double	X[6] =
		{MedianVal( t0 ), MedianVal( t1 ), 0,
		 MedianVal( t3 ), MedianVal( t4 ), 0};

	return Tfmval( X );
}

/* --------------------------------------------------------------- */
/* MapFromZtoMedianTfmval ---------------------------------------- */
/* --------------------------------------------------------------- */

static void MapFromZtoMedianTfmval(
	map<int,Tfmval>			&mzT,
	const vector<double>	&X,
	const vector<zsort>		&zs )
{
	CLayerTfmvalCalc	LC;
	int					nr		= vRgn.size(),
						zcur	= -1;

// Loop over all RGN in z-order and compute median values

	for( int i = 0; i < nr; ++i ) {

		// If new layer then finish prev layer

		if( zs[i].z != zcur ) {

			if( LC.Size() ) {
				mzT[zcur] = LC.LayerVals();
				LC.Reset();
			}

			zcur = zs[i].z;
		}

		// get values for this tile

		const RGN&	I = vRgn[zs[i].i];

		if( I.itr >= 0 )
			LC.Add( &X[0] + I.itr * 6 );
	}

	if( LC.Size() )
		mzT[zcur] = LC.LayerVals();
}

/* --------------------------------------------------------------- */
/* MarkWildItrsInvalid ------------------------------------------- */
/* --------------------------------------------------------------- */

// Return true if any regions marked invalid here.
//
static bool MarkWildItrsInvalid(
	map<int,Tfmval>			&mzT,
	const vector<double>	&X,
	const vector<zsort>		&zs )
{
	FILE	*f	= FileOpenOrDie( "WildTFormTiles.txt", "w" );
	int		nr	= vRgn.size();
	bool	marked = false;

	Tfmval::PrintHdr( f );

	for( int i = 0; i < nr; ++i ) {

		RGN&	I = vRgn[zs[i].i];

		if( I.itr < 0 )
			continue;

		Tfmval	T( &X[0] + I.itr * 6 );

		if( T.IsOutlier( mzT.find( I.z )->second,
				gArgs.tfm_tol, I.z, I.id, f ) ) {

			I.itr	= -1;
			marked	= true;
		}
	}

	fclose( f );

// Repack transform indices (itr)

	if( marked ) {

		gNTr = 0;

		for( int i = 0; i < nr; ++i ) {

			RGN&	I = vRgn[zs[i].i];

			if( I.itr >= 0 )
				I.itr = gNTr++;
		}
	}

	return marked;
}

/* --------------------------------------------------------------- */
/* KillOulierTForms ---------------------------------------------- */
/* --------------------------------------------------------------- */

// After solving for transforms, calculate the median tform values
// for each layer, and then set (used = false) for each constraint
// referencing a tile whose tform is greater than tfm_tol from the
// median.
//
// Return true if any changes made here.
//
static bool KillOulierTForms(
	const vector<double>	&X,
	const vector<zsort>		&zs )
{
	map<int,Tfmval>	mzT;	// z-layer maps to median Tfmval

	MapFromZtoMedianTfmval( mzT, X, zs );

	bool changed = MarkWildItrsInvalid( mzT, X, zs );

// Disable referring constraints

	if( changed ) {

		int	nc = vAllC.size();

		for( int i = 0; i < nc; ++i ) {

			Constraint	&C = vAllC[i];

			if( C.used ) {

				if( vRgn[C.r1].itr < 0 || vRgn[C.r2].itr < 0 )
					C.used = false;
			}
		}
	}

	return changed;
}

/* --------------------------------------------------------------- */
/* IterateInliers ------------------------------------------------ */
/* --------------------------------------------------------------- */

static void IterateInliers(
	vector<double>		&X,
	const vector<zsort>	&zs )
{
	printf( "---- Iterative solver ----\n" );

/* -------------------------- */
/* Init and count constraints */
/* -------------------------- */

	int	nsame	= 0,
		ndiff	= 0,
		nc		= vAllC.size();

	for( int i = 0; i < nc; ++i ) {

		Constraint	&C = vAllC[i];

		if( C.used ) {

			C.inlier = true;

			if( vRgn[C.r1].z == vRgn[C.r2].z )
				++nsame;
			else
				++ndiff;
		}
		else
			C.inlier = false;
	}

	printf(
	"Constraints: %d same-layer; %d diff-layer.\n", nsame, ndiff );

/* ---------------------------------------- */
/* Repeat while any new inliers or outliers */
/* ---------------------------------------- */

	double	lastrms, lastin, lastout;
	int		NewInlier  = 1;
	int		NewOutlier = 0;

	for( int pass = 1;
		pass <= gArgs.max_pass && (NewInlier || NewOutlier);
		++pass ) {

		printf( "\nPASS %d >>>>>>>>\n", pass );

		/* ----- */
		/* Solve */
		/* ----- */

		SolveSystem( X );
//		SolveSystemRigid( X );

		/* -------------------------- */
		/* Apply transform uniformity */
		/* -------------------------- */

		if( pass == 1 && gArgs.tfm_tol > 0.0 ) {

			if( KillOulierTForms( X, zs ) ) {
				printf( "\nPASS %d (Wild TFs Rmvd) >>>>>>>>\n", pass );
				SolveSystem( X );
//				SolveSystemRigid( X );
			}
		}

		/* -------------------------- */
		/* Count inliers and outliers */
		/* -------------------------- */

		NewInlier = 0;
		NewOutlier = 0;

		double	sum		= 0.0,
				big_in	= 0.0,
				big_out	= 0.0;
		int		in		= 0,
				out		= 0;

		for( int i = 0; i < nc; ++i ) {

			Constraint	&C = vAllC[i];

			if( !C.used )
				continue;

			/* ----------------------------- */
			/* Global space points and error */
			/* ----------------------------- */

			TAffine	T1( &X[vRgn[C.r1].itr * 6] ),
					T2( &X[vRgn[C.r2].itr * 6] );
			Point	g1 = C.p1,
					g2 = C.p2;

			T1.Transform( g1 );
			T2.Transform( g2 );

			double	err = g2.DistSqr( g1 );
			bool	old = C.inlier;

			if( C.inlier = (sqrt( err ) <= gArgs.thresh) ) {

				sum   += err;
				big_in = max( big_in, err );

				++in;
				NewInlier += !old;
			}
			else {
				big_out = max( big_out, err );

				++out;
				NewOutlier += old;
			}
		}

		/* ------- */
		/* Reports */
		/* ------- */

		printf( "\n\t\t\t\t"
		"%d new inliers; %d new outliers.\n",
		NewInlier, NewOutlier );

		printf( "\t\t\t\t"
		"%d active constraints;"
		" %d inliers (%.2f%%),"
		" %d outliers (%.2f%%).\n",
		in + out,
		in,  double(in )/(in+out)*100.0,
		out, double(out)/(in+out)*100.0 );

		// Overall error

		lastrms = sqrt( sum / in );
		lastin  = sqrt( big_in );
		lastout = sqrt( big_out );

		const char	*flag;

		if( lastrms > 20.0 )
			flag = "<---------- rms!";
		else if( lastin > 75.0 )
			flag = "<---------- big!";
		else
			flag = "";

		printf( "\t\t\t\t"
		"RMS error %.2f, max error inlier %.2f, max outlier %.2f %s\n",
		lastrms, lastin, lastout, flag );
	}

	printf( "\nFINAL RMS %.2f MAXIN %.2f MAXOUT %.2f\n\n",
	lastrms, lastin, lastout );
}

/* --------------------------------------------------------------- */
/* ApplyLens ----------------------------------------------------- */
/* --------------------------------------------------------------- */

static void ApplyLens( vector<double> &X, bool inv )
{
	if( !gArgs.lens )
		return;

	CAffineLens	LN;
	int			nr = vRgn.size();

	if( !LN.ReadIDB( idb ) )
		exit( 42 );

	for( int i = 0; i < nr; ++i ) {

		int	itr = vRgn[i].itr;

		if( itr < 0 )
			continue;

		LN.UpdateDoublesRHS( &X[itr * 6], vRgn[i].GetName(), inv );
	}
}

/* --------------------------------------------------------------- */
/* Bounds -------------------------------------------------------- */
/* --------------------------------------------------------------- */

static void Bounds(
	double			&xbnd,
	double			&ybnd,
	vector<double>	&X )
{
	printf( "---- Global bounds ----\n" );

// Transform each included regions's rectangle to global
// space, including any global rotation (degcw) and find
// bounds over whole set.

	double	xmin, xmax, ymin, ymax;
	int		nr = vRgn.size();

	if( gArgs.lrbt.size() ) {

		xmin = gArgs.lrbt[0];
		xmax = gArgs.lrbt[1];
		ymin = gArgs.lrbt[2];
		ymax = gArgs.lrbt[3];
	}
	else {

		TAffine	T, R;

		xmin =  BIGD;
		xmax = -BIGD;
		ymin =  BIGD;
		ymax = -BIGD;

		if( gArgs.degcw )
			R.SetCWRot( gArgs.degcw, Point(0,0) );

		for( int i = 0; i < nr; ++i ) {

			int	itr = vRgn[i].itr;

			if( itr < 0 )
				continue;

			TAffine			t( &X[itr * 6] );
			vector<Point>	cnr( 4 );

			T = R * t;
			T.CopyOut( &X[itr * 6] );

			cnr[0] = Point(  0.0, 0.0 );
			cnr[1] = Point( gW-1, 0.0 );
			cnr[2] = Point( gW-1, gH-1 );
			cnr[3] = Point(  0.0, gH-1 );

			T.Transform( cnr );

			for( int k = 0; k < 4; ++k ) {

				xmin = fmin( xmin, cnr[k].x );
				xmax = fmax( xmax, cnr[k].x );
				ymin = fmin( ymin, cnr[k].y );
				ymax = fmax( ymax, cnr[k].y );
			}
		}
	}

	printf( "Propagate bounds with option -lrbt=%f,%f,%f,%f\n\n",
	xmin, xmax, ymin, ymax );

// Translate all transforms to put global origin at ~(0,0).

	for( int i = 0; i < nr; ++i ) {

		int	j = vRgn[i].itr;

		if( j >= 0 ) {
			j		*= 6;
			X[j+2]	-= xmin;
			X[j+5]	-= ymin;
		}
	}

	xmax = ceil( xmax - xmin + 1 );
	ymax = ceil( ymax - ymin + 1 );
	xmin = 0;
	ymin = 0;

// Open GNUPLOT files for debugging

	FILE	*fEven		= FileOpenOrDie( "pf.even", "w" ),
			*fOdd		= FileOpenOrDie( "pf.odd", "w" ),
			*fLabEven	= FileOpenOrDie( "pf.labels.even", "w" ),
			*fLabOdd	= FileOpenOrDie( "pf.labels.odd", "w" );

// Write rects and labels

	for( int i = 0; i < nr; ++i ) {

		int	itr = vRgn[i].itr;

		if( itr < 0 )
			continue;

		TAffine			T( &X[itr * 6] );
		vector<Point>	cnr( 4 );
		double			xmid = 0.0, ymid = 0.0;

		cnr[0] = Point(  0.0, 0.0 );
		cnr[1] = Point( gW-1, 0.0 );
		cnr[2] = Point( gW-1, gH-1 );
		cnr[3] = Point(  0.0, gH-1 );

		T.Transform( cnr );

		for( int k = 0; k < 4; ++k ) {
			xmid += cnr[k].x;
			ymid += cnr[k].y;
		}

		xmid /= 4.0;
		ymid /= 4.0;

		// select even or odd reporting

		FILE	*f, *flab;
		int		color;

		if( vRgn[i].z & 1 ) {
			f		= fOdd;
			flab	= fLabOdd;
			color	= 1;
		}
		else {
			f		= fEven;
			flab	= fLabEven;
			color	= 2;
		}

		// transformed rect

		for( int k = 0; k < 5; ++k )
			fprintf( f, "%f %f\n", cnr[k%4].x, cnr[k%4].y );

		fprintf( f, "\n" );

		// label

		fprintf( flab, "set label \"%d:%d.%d \" at %f,%f tc lt %d\n",
		vRgn[i].z, vRgn[i].id, vRgn[i].rgn, xmid, ymid, color );
	}

// Close files

	fclose( fLabOdd );
	fclose( fLabEven );
	fclose( fOdd );
	fclose( fEven );

// Report

	fprintf( FOUT, "BBOX %f %f %f %f\n", xmin, ymin, xmax, ymax );

	printf( "Bounds of global image are x=[%f %f] y=[%f %f].\n\n",
	xmin, xmax, ymin, ymax );

	xbnd = xmax;
	ybnd = ymax;
}

/* --------------------------------------------------------------- */
/* WriteTransforms ----------------------------------------------- */
/* --------------------------------------------------------------- */

static void WriteTransforms(
	const vector<zsort>		&zs,
	const vector<double>	&X )
{
	printf( "---- Write transforms ----\n" );

	FILE	*f   = FileOpenOrDie( "TAffineTable.txt", "w" );
	double	smin = 100.0,
			smax = 0.0,
			smag = 0.0;
	int		nr   = vRgn.size();

	for( int i = 0; i < nr; ++i ) {

		const RGN&	I = vRgn[zs[i].i];

		if( I.itr < 0 )
			continue;

		int	j = I.itr * 6;

		fprintf( f, "%d\t%d\t%d\t%f\t%f\t%f\t%f\t%f\t%f\n",
		I.z, I.id, I.rgn,
		X[j  ], X[j+1], X[j+2],
		X[j+3], X[j+4], X[j+5] );

		if( !gArgs.strings ) {

			fprintf( FOUT, "TRANSFORM2 %d.%d:%d %f %f %f %f %f %f\n",
			I.z, I.id, I.rgn,
			X[j  ], X[j+1], X[j+2],
			X[j+3], X[j+4], X[j+5] );
		}
		else {
			fprintf( FOUT, "TRANSFORM '%s::%d' %f %f %f %f %f %f\n",
			I.GetName(), I.rgn,
			X[j  ], X[j+1], X[j+2],
			X[j+3], X[j+4], X[j+5] );
		}

		double	mag = sqrt( X[j]*X[j+4] - X[j+1]*X[j+3] );

		smag += mag;
		smin  = fmin( smin, mag );
		smax  = fmax( smax, mag );
	}

	fclose( f );

	printf(
	"Average magnitude=%f, min=%f, max=%f, max/min=%f\n\n",
	smag/gNTr, smin, smax, smax/smin );
}

/* --------------------------------------------------------------- */
/* WriteTrakEM --------------------------------------------------- */
/* --------------------------------------------------------------- */

static void WriteTrakEM(
	double					xmax,
	double					ymax,
	const vector<zsort>		&zs,
	const vector<double>	&X )
{
	FILE	*f = FileOpenOrDie( "MultLayAff.xml", "w" );

	int	oid = 3;

	fprintf( f, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n" );

	TrakEM2WriteDTD( f );

	fprintf( f, "<trakem2>\n" );

	fprintf( f,
	"\t<project\n"
	"\t\tid=\"0\"\n"
	"\t\ttitle=\"Project\"\n"
	"\t\tmipmaps_folder=\"trakem2.mipmaps/\"\n"
	"\t\tn_mipmap_threads=\"8\"\n"
	"\t/>\n" );

	fprintf( f,
	"\t<t2_layer_set\n"
	"\t\toid=\"%d\"\n"
	"\t\ttransform=\"matrix(1.0,0.0,0.0,1.0,0.0,0.0)\"\n"
	"\t\ttitle=\"Top level\"\n"
	"\t\tlayer_width=\"%.2f\"\n"
	"\t\tlayer_height=\"%.2f\"\n"
	"\t>\n",
	oid++, xmax, ymax );

	int	prev	= -1;	// will be previously written layer
	int	offset	= int(2 * gArgs.trim + 0.5);
	int	nr		= vRgn.size();

	for( int i = 0; i < nr; ++i ) {

		const RGN&	I = vRgn[zs[i].i];

		// skip unused tiles
		if( I.itr < 0 )
			continue;

		// changed layer
		if( zs[i].z != prev ) {

			if( prev != -1 )
				fprintf( f, "\t\t</t2_layer>\n" );

			fprintf( f,
			"\t\t<t2_layer\n"
			"\t\t\toid=\"%d\"\n"
			"\t\t\tthickness=\"0\"\n"
			"\t\t\tz=\"%d\"\n"
			"\t\t>\n",
			oid++, zs[i].z );

			prev = zs[i].z;
		}

		// trim trailing quotes and '::'
		// s = filename only
		char		buf[2048];
		strcpy( buf, I.GetName() );
		char		*p = strtok( buf, " ':\n" );
		const char	*s1 = FileNamePtr( p ),
					*s2	= FileDotPtr( s1 );

		// fix origin : undo trimming
		int		j = I.itr * 6;
		double	x = gArgs.trim;
		double	x_orig = X[j  ]*x + X[j+1]*x + X[j+2];
		double	y_orig = X[j+3]*x + X[j+4]*x + X[j+5];

		fprintf( f,
		"\t\t\t<t2_patch\n"
		"\t\t\t\toid=\"%d\"\n"
		"\t\t\t\twidth=\"%d\"\n"
		"\t\t\t\theight=\"%d\"\n"
		"\t\t\t\ttransform=\"matrix(%f,%f,%f,%f,%f,%f)\"\n"
		"\t\t\t\ttitle=\"%.*s\"\n"
		"\t\t\t\ttype=\"%d\"\n"
		"\t\t\t\tfile_path=\"%s\"\n"
		"\t\t\t\to_width=\"%d\"\n"
		"\t\t\t\to_height=\"%d\"\n",
		oid++, gW - offset, gH - offset,
		X[j], X[j+3], X[j+1], X[j+4], x_orig, y_orig,
		s2 - s1, s1, gArgs.xml_type, p, gW - offset, gH - offset );

		if( gArgs.xml_min < gArgs.xml_max ) {

			fprintf( f,
			"\t\t\t\tmin=\"%d\"\n"
			"\t\t\t\tmax=\"%d\"\n"
			"\t\t\t/>\n",
			gArgs.xml_min, gArgs.xml_max );
		}
		else
			fprintf( f, "\t\t\t/>\n" );
	}

	if( nr > 0 )
		fprintf( f, "\t\t</t2_layer>\n" );

	fprintf( f, "\t</t2_layer_set>\n" );
	fprintf( f, "</trakem2>\n" );
	fclose( f );
}

/* --------------------------------------------------------------- */
/* WriteJython --------------------------------------------------- */
/* --------------------------------------------------------------- */

static void WriteJython(
	const vector<zsort>		&zs,
	const vector<double>	&X )
{
	FILE	*f = FileOpenOrDie( "JythonTransforms.txt", "w" );

	fprintf( f, "transforms = {\n" );

	int	nr = vRgn.size();

	for( int i = 0, itrf = 0; i < nr; ++i ) {

		const RGN&	I = vRgn[zs[i].i];

		// skip unused tiles
		if( I.itr < 0 )
			continue;

		++itrf;

		// trim trailing quotes and '::'
		char	buf[2048];
		strcpy( buf, I.GetName() );
		char	*p = strtok( buf, " ':\n" );

		// fix origin : undo trimming
		int		j = I.itr * 6;
		double	x = gArgs.trim;
		double	x_orig = X[j  ]*x + X[j+1]*x + X[j+2];
		double	y_orig = X[j+3]*x + X[j+4]*x + X[j+5];

		fprintf( f, "\"%s\" : [%f, %f, %f, %f, %f, %f]%s\n",
			p, X[j], X[j+3], X[j+1], X[j+4], x_orig, y_orig,
			(itrf == gNTr ? "" : ",") );
	}

	fprintf( f, "}\n" );
	fclose( f );
}

/* --------------------------------------------------------------- */
/* AontoBOverlap ------------------------------------------------- */
/* --------------------------------------------------------------- */

// Return approximated fraction of a on b area overlap.
//
static double AontoBOverlap( TAffine &a, TAffine &b )
{
	TAffine			T;	// T = a->b
	vector<Point>	corners( 4 );

	T.FromAToB( a, b );

	corners[0] = Point(  0.0, 0.0 );
	corners[1] = Point( gW-1, 0.0 );
	corners[2] = Point( gW-1, gH-1 );
	corners[3] = Point(  0.0, gH-1 );

// bounding box.

	double xmin = 1E9, xmax = -1E9;
	double ymin = 1E9, ymax = -1E9;

	for( int k = 0; k < 4; ++k ) {

		T.Transform( corners[k] );

		xmin = fmin( xmin, corners[k].x );
		ymin = fmin( ymin, corners[k].y );
		xmax = fmax( xmax, corners[k].x );
		ymax = fmax( ymax, corners[k].y );
	}

// any overlap possibility?

	if( xmin > gW-1 || xmax < 0 || ymin > gH-1 || ymax < 0 )
		return 0.0;

// approximate area using sampling of random b-points.

	const int count = 4000;

	double	wf	= double(gW-1) / RAND_MAX;
	double	hf	= double(gH-1) / RAND_MAX;
	int		in	= 0;

	for( int i = 0; i < count; ++i ) {

		Point p( wf*rand(), hf*rand() );

		T.Transform( p );

		if( p.x >= 0 && p.x < gW && p.y >= 0.0 && p.y < gH )
			++in;
	}

	//printf( "----AontoBOverlap fraction %f\n", double(in)/count );

	return double(in)/count;
}

/* --------------------------------------------------------------- */
/* NoCorrs ------------------------------------------------------- */
/* --------------------------------------------------------------- */

// Examine region pairs having ZERO corr points between,
// but that might be expected to have connections.
//
// Note that these do not appear in the r12Bad or ignore lists.
// To get into either of those you had to be in the cnxTbl, and
// entries in the cnxTbl come only from 'POINT' entries. So our
// interesting cases are not among those. Moreover, we can skip
// cases having (itr < 0) because, again, those reflect r12Bad
// and ignore listings.
//
static void NoCorrs(
	const vector<zsort>		&zs,
	const vector<double>	&X )
{
	printf( "---- Check NoCorrs ----\n" );

	FILE	*fscr = FileOpenOrDie( "NoCorr", "w" ),
			*flog = FileOpenOrDie( "NoCorrLog", "w" );

/* ------------------------ */
/* Look at each region i... */
/* ------------------------ */

	int	nr = vRgn.size(), nreports = 0;

	for( int i = 0; i < nr; ++i ) {

		int i1 = zs[i].i,
			z1 = zs[i].z;

		fprintf( fscr, "#Start region %d, layer %d\n", i1, z1 );

		const RGN	&A = vRgn[i1];

		if( A.itr < 0 )
			continue;

		/* ---------------------------------------------- */
		/* ...Against each region j in same or next layer */
		/* ---------------------------------------------- */

		for( int j = i+1; j < nr && zs[j].z <= z1+1; ++j ) {

			int i2 = zs[j].i;

			const RGN	&B = vRgn[i2];

			if( B.itr < 0 )
				continue;

			// diff only by rgn?
			if( z1 == zs[j].z && A.id == B.id )
				continue;

			// mapped pairs not interesting here
			if( r12Idx.find( CRPair( i1, i2 ) ) != r12Idx.end() )
				continue;

			/* ------------------------- */
			/* OK, this was never a pair */
			/* ------------------------- */

			TAffine	t1( &X[A.itr * 6] ),
					t2( &X[B.itr * 6] );
			double	olap = AontoBOverlap( t1, t2 );

			if( olap <= 0.25 )
				continue;

			/* ----------------------- */
			/* But there is overlap... */
			/* ----------------------- */

			// Count conRgns for each

			int	nr1 = nConRgn[MZID( A.z, A.id )],
				nr2	= nConRgn[MZID( B.z, B.id )];

			// only consider cases without folds
			if( nr1 != 1 || nr2 != 1 )
				continue;

			/* ---------------- */
			/* Report this case */
			/* ---------------- */

			++nreports;

			fprintf( flog, "No points in common -"
			" Lyr.til:rgn %d.%d:%d - %d.%d:%d, overlap %.1f%%\n"
			" - %s\n"
			" - %s\n",
			A.z, A.id, A.rgn, B.z, B.id, B.rgn, olap*100.0,
			A.GetName(), B.GetName() );

			/* ---------------- */
			/* Report in NoCorr */
			/* ---------------- */

			// Create:
			// forward = t1 -> t2
			// reverse = t2 -> t1

			TAffine	forward, reverse;

			forward.FromAToB( t1, t2 );
			reverse.InverseOf( forward );

			/* ---------------------------------------- */
			/* Instructions to redo A onto B (1 onto 2) */
			/* ---------------------------------------- */

			fprintf( fscr,
			"cd %d\n"
			"rm %d/%d.%d.*\n", A.z, A.id, B.z, B.id );

			fprintf( fscr, "#Transform 1->2: " );
			forward.TPrintAsParam( fscr, true );

			fprintf( fscr, "make -f make.up EXTRA='-F=../param.redo" );
			forward.TPrintAsParam( fscr );
			fprintf( fscr, "'\n" );

			fprintf( fscr, "cd ..\n" );

			/* ---------------------------------------- */
			/* Instructions to redo B onto A (2 onto 1) */
			/* ---------------------------------------- */

			fprintf( fscr,
			"cd %d\n"
			"rm %d/%d.%d.*\n", B.z, B.id, A.z, A.id );

			fprintf( fscr, "#Transform 2->1: " );
			reverse.TPrintAsParam( fscr, true );

			fprintf( fscr, "make -f make.down EXTRA='-F=../param.redo" );
			reverse.TPrintAsParam( fscr );
			fprintf( fscr, "'\n" );

			fprintf( fscr, "cd ..\n");
		}
	}

	fclose( flog );
	fclose( fscr );

	printf( "%d NoCorr cases reported.\n\n", nreports );
}

/* --------------------------------------------------------------- */
/* Tabulate ------------------------------------------------------ */
/* --------------------------------------------------------------- */

// Loop over all constraints (point-pairs) and---
//
// - convert constraint's points to global space
// - compute err = (p2-p1)^2 in global space
// - Store errs in Epnt[]
// - Sum energies = err^2 in Ergn[]
// - Record worst error data for whole layers and layer-pairs
// - Enter error in 'PostFitErrs.txt'
//
// Report some summary results in log.
//
void EVL::Tabulate(
	const vector<zsort>		&zs,
	const vector<double>	&X )
{
	FILE			*f		= FileOpenOrDie( "PostFitErrs.txt", "w" );
	int				nr		= vRgn.size(),
					nc		= vAllC.size();
	vector<Error>	Ergn( nr );	// total error, by region
	double			sum		= 0.0,
					biggest	= 0.0;

// 'PostFitErrs.txt' headers

	fprintf( f, "LyrA\tTilA\tRgnA\tLyrB\tTilB\tRgnB\tSqrErr\n" );

// Init region energies

	for( int i = 0; i < nr; ++i ) {
		Ergn[i].amt	= 0.0;
		Ergn[i].idx	= i;
	}

// Init whole layer data (size: max_layer_id + 1)

	Ein.resize( zs[zs.size()-1].z + 1 );
	Ebt = Ein;

// Tabulate errors per constraint and per region

	for( int i = 0; i < nc; ++i ) {

		double		err;
		Constraint	&C = vAllC[i];

		if( !C.used )
			err = -2;
		else if( !C.inlier )
			err = -1;
		else {

			/* ----------------------------- */
			/* Global space points and error */
			/* ----------------------------- */

			TAffine	T1( &X[vRgn[C.r1].itr * 6] ),
					T2( &X[vRgn[C.r2].itr * 6] );
			Point	&g1 = C.p1,
					&g2 = C.p2;

			T1.Transform( g1 );
			T2.Transform( g2 );

			err = g2.DistSqr( g1 );

			/* --------- */
			/* Reporting */
			/* --------- */

			sum += err;
			biggest = max( biggest, err );

			fprintf( FOUT, "MPOINTS %d %f %f %d %f %f\n",
			vRgn[C.r1].z, g1.x, g1.y,
			vRgn[C.r2].z, g2.x, g2.y );

			/* ------ */
			/* Epnt[] */
			/* ------ */

			Epnt.push_back( Error( err, i ) );

			/* ------ */
			/* Ergn[] */
			/* ------ */

			Ergn[C.r1].amt += err;
			Ergn[C.r2].amt += err;

			/* ----------- */
			/* Whole layer */
			/* ----------- */

			SecErr	*S;
			int		z1 = vRgn[C.r1].z,
					z2 = vRgn[C.r2].z;

			if( z1 == z2 )
				S = &Ein[z1];
			else
				S = &Ebt[min( z1, z2 )];

			if( err > S->err )
				*S = SecErr( g1, g2, err, i );
		}

		/* ---- */
		/* File */
		/* ---- */

		fprintf( f, "%d\t%d\t%d\t%d\t%d\t%d\t%f\n",
		vRgn[C.r1].z, vRgn[C.r1].id, vRgn[C.r1].rgn,
		vRgn[C.r2].z, vRgn[C.r2].id, vRgn[C.r2].rgn, err );
	}

// Close file

	fclose( f );

// Print overall error

	int			istart,
				iend	= Epnt.size();
	double		rms		= sqrt( sum / iend ),
				big		= sqrt( biggest );
	const char	*flag;

	if( rms > 20.0 )
		flag = "<---------- rms!";
	else if( big > 75.0 )
		flag = "<---------- big!";
	else
		flag = "";

	printf( "%d transforms, RMS error %.2f, max error %.2f %s\n\n",
	gNTr, rms, big, flag );

// Print 10 biggest errors

	printf( "Ten largest constraint errors---\n" );
	printf( "Error\n" );

	sort( Epnt.begin(), Epnt.end() );

	istart = max( 0, iend - 10 );

	for( int i = istart; i < iend; ++i )
		printf( "%f\n", sqrt( Epnt[i].amt ) );

	printf( "\n" );

// Print regions with largest strain energies

	printf( "Ten largest region energies---\n" );
	printf( "      Energy\tLayer\tTile\t Rgn\tName\n" );

	sort( Ergn.begin(), Ergn.end() );

	iend	= Ergn.size();
	istart	= max( 0, iend - 10 );

	for( int i = istart; i < iend; ++i ) {

		const Error	&E = Ergn[i];
		const RGN	&I = vRgn[E.idx];

		printf( "%12.1f\t%4d\t%4d\t%4d\t%s\n",
		E.amt, I.z, I.id, I.rgn, I.GetName() );
	}

	printf( "\n" );
}

/* --------------------------------------------------------------- */
/* Line ---------------------------------------------------------- */
/* --------------------------------------------------------------- */

void EVL::Line(
	FILE	*f,
	double	xfrom,
	double	yfrom,
	double	xto,
	double	yto )
{
	fprintf( f, "\n%f %f\n%f %f\n", xfrom, yfrom, xto, yto );
}

/* --------------------------------------------------------------- */
/* BoxOrCross ---------------------------------------------------- */
/* --------------------------------------------------------------- */

// Draw a box or a cross at the specified location.
//
void EVL::BoxOrCross( FILE *f, double x, double y, bool box )
{
	if( box ) {
		fprintf( f, "\n%f %f\n%f %f\n", x-20, y-20, x-20, y+20 );
		fprintf( f,   "%f %f\n%f %f\n", x-20, y+20, x+20, y+20 );
		fprintf( f,   "%f %f\n%f %f\n", x+20, y+20, x+20, y-20 );
		fprintf( f,   "%f %f\n%f %f\n", x+20, y-20, x-20, y-20 );
	}
	else {	// otherwise draw a cross
		Line( f, x-20, y-20, x+20, y+20 );
		Line( f, x-20, y+20, x+20, y-20 );
	}
}

/* --------------------------------------------------------------- */
/* Arrow --------------------------------------------------------- */
/* --------------------------------------------------------------- */

// Draw arrow head '>' pointing at g2, from g1-direction.
//
void EVL::Arrow( FILE *f, const Point &g1, const Point &g2 )
{
	double	q, x, y, dx, dy, c, s;

// 30 pixel length
// 50 degree opening angle

	dx = g1.x - g2.x;
	dy = g1.y - g2.y;
	q  = 30 / sqrt( dx*dx + dy*dy );
	s  = 25 * PI / 180;
	c  = cos( s );
	s  = sin( s );

	x  = q * (c*dx - s*dy) + g2.x;
	y  = q * (s*dx + c*dy) + g2.y;

	Line( f, x, y, g2.x, g2.y );

	s  = -s;
	x  = q * (c*dx - s*dy) + g2.x;
	y  = q * (s*dx + c*dy) + g2.y;

	Line( f, x, y, g2.x, g2.y );
}

/* --------------------------------------------------------------- */
/* Print_be_and_se_files ----------------------------------------- */
/* --------------------------------------------------------------- */

// Log the NPRNT biggest errors.
//
// Plot the NPLOT biggest errors, and all those over 75 pixels.
// pf.se contains those that were OK, for contrast.
//
// Display plot files using:
// > gnuplot
// gnuplot> plot 'pf.be' with lines
// gnuplot> exit
//
void EVL::Print_be_and_se_files( const vector<zsort> &zs )
{
	const int NPRNT = 10;
	const int NPLOT = 50;

	FILE	*fbe = FileOpenOrDie( "pf.be", "w" ),
			*fse = FileOpenOrDie( "pf.se", "w" );

	int		ne		= Epnt.size(),
			nc		= vAllC.size();
	double	bigpr	= (ne > NPRNT ? Epnt[ne - NPRNT].amt : 0.0),
			bigpl	= (ne > NPLOT ? Epnt[ne - NPLOT].amt : 0.0);

	printf( "Maximum layer number is %d\n\n", zs[zs.size()-1].z );

	printf( "Ten largest constraint errors---\n" );
	printf( "     Error\tLayer\tTile\t Rgn\tLayer\tTile\t Rgn\n" );

	for( int i = 0; i < nc; ++i ) {

		const Constraint	&C = vAllC[i];

		if( !C.used || !C.inlier )
			continue;

		const Point	&g1 = C.p1,
					&g2 = C.p2;

		double	err = g2.DistSqr( g1 );
		int		z1  = vRgn[C.r1].z,
				z2  = vRgn[C.r2].z;

		// print out if big enough

		if( err >= bigpr ) {

			printf( "%10.1f\t%4d\t%4d\t%4d\t%4d\t%4d\t%4d\n",
			sqrt( err ),
			z1, vRgn[C.r1].id, vRgn[C.r1].rgn,
			z2, vRgn[C.r2].id, vRgn[C.r2].rgn );

			printf( "%s\n%s\n",
			vRgn[C.r1].GetName(), vRgn[C.r2].GetName() );
		}

		// and plot

		if( err >= bigpl || sqrt( err ) > 75.0 ) {

			Line( fbe, g1.x, g1.y, g2.x, g2.y );

			BoxOrCross( fbe, g1.x, g1.y, !(z1 & 1) );
			BoxOrCross( fbe, g2.x, g2.y, !(z2 & 1) );

			Arrow( fbe, g1, g2 );
		}
		else
			Line( fse, g1.x, g1.y, g2.x, g2.y );
	}

	fclose( fbe );
	fclose( fse );
}

/* --------------------------------------------------------------- */
/* Print_errs_by_layer ------------------------------------------- */
/* --------------------------------------------------------------- */

void EVL::Print_errs_by_layer( const vector<zsort> &zs )
{
	FILE	*f = FileOpenOrDie( "errs_by_layer.txt", "w" );

	int	zmax = zs[zs.size()-1].z;

	for( int i = zs[0].z; i <= zmax; i++ ) {

		const SecErr	&Ei = Ein[i];
		const SecErr	&Eb = Ebt[i];

		int	it1 = 0, it2 = 0,	// in-layer tiles
			bt1 = 0, bt2 = 0,	// tween tiles
			bz1 = 0, bz2 = 0;	// tween z's

		if( Ei.idx >= 0 ) {

			const Constraint	&C = vAllC[Ei.idx];

			it1  = vRgn[C.r1].id;
			it2  = vRgn[C.r2].id;
		}

		if( Eb.idx >= 0 ) {

			const Constraint	&C = vAllC[Eb.idx];

			bz1	= vRgn[C.r1].z;
			bt1	= vRgn[C.r1].id;
			bz2	= vRgn[C.r2].z;
			bt2	= vRgn[C.r2].id;
		}

		fprintf( f,
		"Layer %4d:"
		" %3d:%3d %8.1f at (%8.1f, %8.1f),"
		" %4d:%3d <-> %4d:%3d %8.1f at (%8.1f, %8.1f)\n",
		i,
		it1, it2, sqrt( Ei.err ), Ei.loc.x, Ei.loc.y,
		bz1, bt1, bz2, bt2, sqrt( Eb.err ), Eb.loc.x, Eb.loc.y );
	}

	fclose( f );
}

/* --------------------------------------------------------------- */
/* ViseWriteXML -------------------------------------------------- */
/* --------------------------------------------------------------- */

#define	visePix	128

static void ViseWriteXML(
	double					xmax,
	double					ymax,
	const vector<zsort>		&zs,
	const vector<double>	&X )
{
	FILE	*f = FileOpenOrDie( "visexml.xml", "w" );

	double	sclx = (double)gW / visePix,
			scly = (double)gH / visePix;
	int		oid  = 3;

	fprintf( f, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n" );

	TrakEM2WriteDTD( f );

	fprintf( f, "<trakem2>\n" );

	fprintf( f,
	"\t<project\n"
	"\t\tid=\"0\"\n"
	"\t\ttitle=\"Project\"\n"
	"\t\tmipmaps_folder=\"trakem2.mipmaps/\"\n"
	"\t\tn_mipmap_threads=\"8\"\n"
	"\t/>\n" );

	fprintf( f,
	"\t<t2_layer_set\n"
	"\t\toid=\"%d\"\n"
	"\t\ttransform=\"matrix(1.0,0.0,0.0,1.0,0.0,0.0)\"\n"
	"\t\ttitle=\"Top level\"\n"
	"\t\tlayer_width=\"%.2f\"\n"
	"\t\tlayer_height=\"%.2f\"\n"
	"\t>\n",
	oid++, xmax, ymax );

	int	prev	= -1;	// will be previously written layer
	int	nr		= vRgn.size();

	for( int i = 0; i < nr; ++i ) {

		const RGN&	I = vRgn[zs[i].i];

		// skip unused tiles
		if( I.itr < 0 )
			continue;

		// changed layer
		if( zs[i].z != prev ) {

			if( prev != -1 )
				fprintf( f, "\t\t</t2_layer>\n" );

			fprintf( f,
			"\t\t<t2_layer\n"
			"\t\t\toid=\"%d\"\n"
			"\t\t\tthickness=\"0\"\n"
			"\t\t\tz=\"%d\"\n"
			"\t\t>\n",
			oid++, zs[i].z );

			prev = zs[i].z;
		}

		char		buf[256];
		const char	*c, *n = FileNamePtr( I.GetName() );

		if( c = strstr( n, "col" ) ) {
			sprintf( buf, "ve_z%d_id%d_%.*s",
			I.z, I.id, strchr( c, '.' ) - c, c );
		}
		else
			sprintf( buf, "ve_z%d_id%d", I.z, I.id );

		int		j = I.itr * 6;

		fprintf( f,
		"\t\t\t<t2_patch\n"
		"\t\t\t\toid=\"%d\"\n"
		"\t\t\t\twidth=\"%d\"\n"
		"\t\t\t\theight=\"%d\"\n"
		"\t\t\t\ttransform=\"matrix(%f,%f,%f,%f,%f,%f)\"\n"
		"\t\t\t\ttitle=\"%s\"\n"
		"\t\t\t\ttype=\"4\"\n"
		"\t\t\t\tfile_path=\"viseimg/%d/%s.png\"\n"
		"\t\t\t\to_width=\"%d\"\n"
		"\t\t\t\to_height=\"%d\"\n"
		"\t\t\t\tmin=\"0\"\n"
		"\t\t\t\tmax=\"255\"\n"
		"\t\t\t/>\n",
		oid++, visePix, visePix,
		sclx*X[j], scly*X[j+3], sclx*X[j+1], scly*X[j+4], X[j+2], X[j+5],
		buf, I.z, buf, visePix, visePix );
	}

	if( nr > 0 )
		fprintf( f,"\t\t</t2_layer>\n");

	fprintf( f, "\t</t2_layer_set>\n");
	fprintf( f, "</trakem2>\n");
	fclose( f );
}

/* --------------------------------------------------------------- */
/* ViseEval1 ----------------------------------------------------- */
/* --------------------------------------------------------------- */

// Each rgn gets a VisErr record that describes errors w.r.t.
// adjacent tiles in same layer {L,R,B,T} and with down {D}.
// In this first pass, each {L,R,B,T} bin gets a negative
// count of the correspondence points that map to that side.
// The issue is to make sure that corner points will map to
// a proper side. Sides having < 3 points are actually empty.
//
void EVL::ViseEval1(
	vector<VisErr>			&ve,
	const vector<double>	&X )
{
	Point	M( gW/2, gH/2 );	// local image center
	int		ne = Epnt.size();
	double	aTL, aTR;			// top-left, right angles

	aTR = atan2( M.y, M.x ) * 180/PI;
	aTL = 180 - aTR;

// zero all {L,R,B,T,D}
	memset( &ve[0], 0, vRgn.size() * sizeof(VisErr) );

	for( int i = 0; i < ne; ++i ) {

		const Constraint	&C = vAllC[Epnt[i].idx];

		int	z1 = vRgn[C.r1].z,
			z2 = vRgn[C.r2].z;

		if( z1 == z2 ) {

			// constraints 1 & 2 done symmetrically
			// so load into arrays and loop

			const Point*	p[2] = {&C.p1, &C.p2};
			int				r[2] = {C.r1, C.r2};

			for( int j = 0; j < 2; ++j ) {

				// the constraint points are in global coords
				// so to see which side-sector a point is in
				// we will get angle of vector from local center
				// to local point

				VisErr	&V = ve[r[j]];
				TAffine	Inv, T( &X[vRgn[r[j]].itr * 6] );
				Point	L = *p[j];
				double	a, *s;

				Inv.InverseOf( T );
				Inv.Transform( L );

				a = atan2( L.y-M.y, L.x-M.x ) * 180/PI;

				// R (-aTR,aTR]
				// T (aTR,aTL]
				// B (-aTL,-aTR]
				// L else

				if( a > -aTL ) {

					if( a > -aTR ) {

						if( a > aTR ) {

							if( a > aTL )
								s = &V.L;
							else
								s = &V.T;
						}
						else
							s = &V.R;
					}
					else
						s = &V.B;
				}
				else
					s = &V.L;

				*s -= 1;	// counts are negative
			}
		}
	}
}

/* --------------------------------------------------------------- */
/* ViseEval2 ----------------------------------------------------- */
/* --------------------------------------------------------------- */

// Each rgn gets a VisErr record that describes errors w.r.t.
// adjacent tiles in same layer {L,R,B,T} and with down {D}.
// In this second pass, we record the maximum error for each
// bin. If a point maps to side with low occupancy from pass
// one, then we remap it to a true side.
//
void EVL::ViseEval2(
	vector<VisErr>			&ve,
	const vector<double>	&X )
{
	Point	M( gW/2, gH/2 );	// local image center
	int		ne = Epnt.size();
	double	aTL, aTR;			// top-left, right angles

	aTR = atan2( M.y, M.x ) * 180/PI;
	aTL = 180 - aTR;

	for( int i = 0; i < ne; ++i ) {

		const Constraint	&C = vAllC[Epnt[i].idx];

		int	z1 = vRgn[C.r1].z,
			z2 = vRgn[C.r2].z;

		if( z1 == z2 ) {

			// constraints 1 & 2 done symmetrically
			// so load into arrays and loop

			const Point*	p[2] = {&C.p1, &C.p2};
			int				r[2] = {C.r1, C.r2};

			for( int j = 0; j < 2; ++j ) {

				// the constraint points are in global coords
				// so to see which side-sector a point is in
				// we will get angle of vector from local center
				// to local point

				VisErr	&V = ve[r[j]];
				TAffine	Inv, T( &X[vRgn[r[j]].itr * 6] );
				Point	L = *p[j];
				double	a, *s;

				Inv.InverseOf( T );
				Inv.Transform( L );

				a = atan2( L.y-M.y, L.x-M.x ) * 180/PI;

				// R (-aTR,aTR]
				// T (aTR,aTL]
				// B (-aTL,-aTR]
				// L else

				if( a > -aTL ) {

					if( a > -aTR ) {

						if( a > aTR ) {

							if( a > aTL )
								s = &V.L;
							else
								s = &V.T;
						}
						else
							s = &V.R;
					}
					else
						s = &V.B;
				}
				else
					s = &V.L;

				// occupancy test and remap

				if( !*s )
					continue;
				else if( *s < -2 || *s > 0 )
					;
				else {
					// remap

					if( s == &V.L ) {

						if( a >= -180 ) {
							if( V.B < -3 || V.B > 0 )
								s = &V.B;
							else
								continue;
						}
						else {
							if( V.T < -3 || V.T > 0 )
								s = &V.T;
							else
								continue;
						}
					}
					else if( s == &V.R ) {

						if( a <= 0 ) {
							if( V.B < -3 || V.B > 0 )
								s = &V.B;
							else
								continue;
						}
						else {
							if( V.T < -3 || V.T > 0 )
								s = &V.T;
							else
								continue;
						}
					}
					else if( s == &V.B ) {

						if( a <= -90 ) {
							if( V.L < -3 || V.L > 0 )
								s = &V.L;
							else
								continue;
						}
						else {
							if( V.R < -3 || V.R > 0 )
								s = &V.R;
							else
								continue;
						}
					}
					else {

						if( a >= 90 ) {
							if( V.L < -3 || V.L > 0 )
								s = &V.L;
							else
								continue;
						}
						else {
							if( V.R < -3 || V.R > 0 )
								s = &V.R;
							else
								continue;
						}
					}
				}

				if( Epnt[i].amt > *s )
					*s = Epnt[i].amt;
			}
		}
		else {

			// for down, we will just lump all together into D

			double	&D = (z1 > z2 ? ve[C.r1].D : ve[C.r2].D);

			if( Epnt[i].amt > D )
				D = Epnt[i].amt;
		}
	}
}

/* --------------------------------------------------------------- */
/* ViseColor ----------------------------------------------------- */
/* --------------------------------------------------------------- */

static uint32 ViseColor( double err )
{
	const uint32	ezero = 0xFF0000FF,
					eover = 0xFF00FFFF;

	uint32	ecolr;

	if( err <= 0 )
		ecolr = ezero;
	else if( err >= gArgs.viserr )
		ecolr = eover;
	else {
		uint32	c = (uint32)(255 * err / gArgs.viserr);
		ecolr = 0xFF000000 + (c << 8);
	}

	return ecolr;
}

/* --------------------------------------------------------------- */
/* VisePaintRect ------------------------------------------------- */
/* --------------------------------------------------------------- */

static void VisePaintRect(
	vector<uint32>	&RGB,
	int				x0,
	int				xlim,
	int				y0,
	int				ylim,
	double			errsqr )
{
	uint32	ecolr = ViseColor( (errsqr > 0 ? sqrt( errsqr ) : 0) );

	++xlim;
	++ylim;

	for( int y = y0; y < ylim; ++y ) {

		for( int x = x0; x < xlim; ++x )
			RGB[x + visePix * y] = ecolr;
	}
}

/* --------------------------------------------------------------- */
/* BuildVise ----------------------------------------------------- */
/* --------------------------------------------------------------- */

// Build a set of error visualization images--
//
// visexml.xml is TrackEM2 file pointing at diagnostic images
// in viseimg/. Each RGB image is 100x100 pixels. The central
// square in each image depicts down errors {red=none, green
// brightness proportional to max error}. The sides of the open
// box in each image are similarly scaled for in-plane errors.
//
void EVL::BuildVise(
	double					xmax,
	double					ymax,
	const vector<zsort>		&zs,
	const vector<double>	&X )
{
	ViseWriteXML( xmax, ymax, zs, X );

	int				nr = vRgn.size();
	vector<VisErr>	ve( nr );

	ViseEval1( ve, X );
	ViseEval2( ve, X );

	DskCreateDir( "viseimg", stdout );

	FILE	*f = FileOpenOrDie( "viseparse.txt", "w" );
	char	buf[256];
	int		prev = -1,	// will be previously written layer
			twv  = visePix / 12;

	fprintf( f, "Z\tID\tCol\tRow\tCam\tL\tR\tB\tT\tD\n" );

	for( int i = 0; i < nr; ++i ) {

		const RGN&	I = vRgn[zs[i].i];

		// skip unused tiles
		if( I.itr < 0 )
			continue;

		// changed layer
		if( zs[i].z != prev ) {
			sprintf( buf, "viseimg/%d", zs[i].z );
			DskCreateDir( buf, stdout );
			prev = zs[i].z;
		}
		// light gray image
		vector<uint32>	RGB( visePix * visePix, 0xFFD0D0D0 );

		const VisErr	&V = ve[zs[i].i];
		const char		*c, *n = FileNamePtr( I.GetName() );
		int				col = 0, row = 0, cam = 0;

		if( c = strstr( n, "col" ) )
			sscanf( c, "col%d_row%d_cam%d", &col, &row, &cam );

		fprintf( f, "%d\t%d\t%d\t%d\t%d\t%f\t%f\t%f\t%f\t%f\n",
		I.z, I.id, col, row, cam,
		(V.L > 0 ? sqrt(V.L) : 0),
		(V.R > 0 ? sqrt(V.R) : 0),
		(V.B > 0 ? sqrt(V.B) : 0),
		(V.T > 0 ? sqrt(V.T) : 0),
		(V.D > 0 ? sqrt(V.D) : 0) );

		// down
		if( zs[i].z != zs[0].z ) {

			VisePaintRect( RGB,
				5 * twv, 7 * twv,
				5 * twv, 7 * twv,
				V.D );
		}

		//left
		VisePaintRect( RGB,
			3 * twv, 4 * twv,
			4 * twv, 8 * twv,
			V.L );

		//right
		VisePaintRect( RGB,
			8 * twv, 9 * twv,
			4 * twv, 8 * twv,
			V.R );

		//bot
		VisePaintRect( RGB,
			4 * twv, 8 * twv,
			3 * twv, 4 * twv,
			V.B );

		//top
		VisePaintRect( RGB,
			4 * twv, 8 * twv,
			8 * twv, 9 * twv,
			V.T );

		// border
		int	lim = visePix - 1;
		VisePaintRect( RGB, 0, twv/2, 0, lim, .01 );
		VisePaintRect( RGB, lim - twv/2, lim, 0, lim, .01 );
		VisePaintRect( RGB, 0, lim, 0, twv/2, .01 );
		VisePaintRect( RGB, 0, lim, lim - twv/2, lim, .01 );

		// store
		if( c ) {
			sprintf( buf, "viseimg/%d/ve_z%d_id%d_%.*s.png",
			I.z, I.z, I.id, strchr( c, '.' ) - c, c );
		}
		else {
			sprintf( buf, "viseimg/%d/ve_z%d_id%d.png",
			I.z, I.z, I.id );
		}

		Raster32ToPngRGBA( buf, &RGB[0], visePix, visePix );
	}

	fclose( f );
}

/* --------------------------------------------------------------- */
/* Evaluate ------------------------------------------------------ */
/* --------------------------------------------------------------- */

void EVL::Evaluate(
	double					xmax,
	double					ymax,
	const vector<zsort>		&zs,
	const vector<double>	&X )
{
	printf( "---- Evaluate errors ----\n" );

	Tabulate( zs, X );
	Print_be_and_se_files( zs );
	Print_errs_by_layer( zs );

	if( gArgs.viserr > 0 )
		BuildVise( xmax, ymax, zs, X );

	printf( "\n" );
}

/* --------------------------------------------------------------- */
/* main ---------------------------------------------------------- */
/* --------------------------------------------------------------- */

int main( int argc, char **argv )
{
/* ------------------ */
/* Parse command line */
/* ------------------ */

	gArgs.SetCmdLine( argc, argv );

/* ------------------ */
/* Create output file */
/* ------------------ */

	FOUT = FileOpenOrDie( "simple", "w" );

/* --------------------- */
/* Read input data files */
/* --------------------- */

// CNX: collect connection data
// RGD: collect data to try rigid alignments

	CNX	*cnx = new CNX;
	RGD	*rgd = new RGD;

	if( gArgs.strings ) {

		ReadPts_StrTags( FOUT, cnx, rgd,
			IDFromName, gArgs.dir_file, gArgs.pts_file );
	}
	else
		ReadPts_NumTags( FOUT, cnx, rgd, gArgs.pts_file );

/* ------------------------- */
/* Try aligning region pairs */
/* ------------------------- */

// This logs reports about suspicious pairs
// but has no other impact on the real solver.

	rgd->TestPairAlignments();

	delete rgd;

/* ------------ */
/* Select model */
/* ------------ */

	switch( gArgs.model ) {
		case 'R': M = new MRigid;  break;
		case 'A': M = new MAffine; break;
		case 'H': M = new MHmgphy; break;
	}

/* ------------------------------------------- */
/* Decide which regions have valid connections */
/* ------------------------------------------- */

// Results mark the global RGN.itr fields

	gNTr = cnx->SelectIncludedImages(
			(gArgs.use_all ? 0 : M->MinLinks()) );

	delete cnx;

/* ----------------- */
/* Sort regions by z */
/* ----------------- */

	int				nr = vRgn.size();
	vector<zsort>	zs( nr );

	for( int i = 0; i < nr; ++i )
		zs[i] = zsort( vRgn[i], i );

	sort( zs.begin(), zs.end() );

/* ----- */
/* Solve */
/* ----- */

// X are the global transform elements;
// six packed doubles per valid region.

	vector<double>	X;

	IterateInliers( X, zs );
	ApplyLens( X, false );

/* ------------------ */
/* Calc global bounds */
/* ------------------ */

	double	xbnd, ybnd;

	Bounds( xbnd, ybnd, X );

/* ---------------- */
/* Write transforms */
/* ---------------- */

	WriteTransforms( zs, X );
	WriteTrakEM( xbnd, ybnd, zs, X );
	WriteJython( zs, X );

/* ---------------------------------- */
/* Report any missing correspondences */
/* ---------------------------------- */

	ApplyLens( X, true );
	NoCorrs( zs, X );

/* ------------------------ */
/* Assess and report errors */
/* ------------------------ */

	EVL	evl;

	evl.Evaluate( xbnd, ybnd, zs, X );

/* ---- */
/* Done */
/* ---- */

	fclose( FOUT );

	return 0;
}


