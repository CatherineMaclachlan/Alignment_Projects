

#include	"Scape.h"
#include	"ImageIO.h"
#include	"Maths.h"


/* --------------------------------------------------------------- */
/* AdjustBounds -------------------------------------------------- */
/* --------------------------------------------------------------- */

static void AdjustBounds(
	uint32			&ws,
	uint32			&hs,
	double			&x0,
	double			&y0,
	vector<ScpTile>	&vTile,
	int				wi,
	int				hi,
	double			scale,
	int				szmult )
{
// maximum extents over all image corners

	double	xmin, xmax, ymin, ymax;
	int		nt = vTile.size();

	xmin =  BIGD;
	xmax = -BIGD;
	ymin =  BIGD;
	ymax = -BIGD;

	for( int i = 0; i < nt; ++i ) {

		vector<Point>	cnr( 4 );

		cnr[0] = Point(  0.0, 0.0 );
		cnr[1] = Point( wi-1, 0.0 );
		cnr[2] = Point( wi-1, hi-1 );
		cnr[3] = Point(  0.0, hi-1 );

		vTile[i].t2g.Transform( cnr );

		for( int k = 0; k < 4; ++k ) {

			xmin = fmin( xmin, cnr[k].x );
			xmax = fmax( xmax, cnr[k].x );
			ymin = fmin( ymin, cnr[k].y );
			ymax = fmax( ymax, cnr[k].y );
		}
	}

// scale, and expand out to integer bounds

	x0 = xmin * scale;
	y0 = ymin * scale;
	ws = (int)ceil( (xmax - xmin + 1) * scale );
	hs = (int)ceil( (ymax - ymin + 1) * scale );

// ensure dims divisible by szmult

	int	rem;

	if( rem = ws % szmult )
		ws += szmult - rem;

	if( rem = hs % szmult )
		hs += szmult - rem;

// propagate new dims

	TAffine	A;

	A.NUSetScl( scale );

	for( int i = 0; i < nt; ++i ) {

		TAffine	&T = vTile[i].t2g;

		T = A * T;
		T.AddXY( -x0, -y0 );
	}
}

/* --------------------------------------------------------------- */
/* ScanLims ------------------------------------------------------ */
/* --------------------------------------------------------------- */

// Fill in the range of coords [x0,xL); [y0,yL) in the scape
// that the given tile will occupy. This tells the painter
// which region to fill in.
//
// We expand the tile by one pixel to be conservative, and
// transform the tile bounds to scape coords. In no case are
// the scape coords to exceed [0,ws); [0,hs).
//
static void ScanLims(
	int				&x0,
	int				&xL,
	int				&y0,
	int				&yL,
	int				ws,
	int				hs,
	const TAffine	&T,
	int				wi,
	int				hi )
{
	double	xmin, xmax, ymin, ymax;

	xmin =  BIGD;
	xmax = -BIGD;
	ymin =  BIGD;
	ymax = -BIGD;

	vector<Point>	cnr( 4 );

// generous box (outset 1 pixel) for scanning

	cnr[0] = Point( -1.0, -1.0 );
	cnr[1] = Point(   wi, -1.0 );
	cnr[2] = Point(   wi,   hi );
	cnr[3] = Point( -1.0,   hi );

	T.Transform( cnr );

	for( int k = 0; k < 4; ++k ) {

		xmin = fmin( xmin, cnr[k].x );
		xmax = fmax( xmax, cnr[k].x );
		ymin = fmin( ymin, cnr[k].y );
		ymax = fmax( ymax, cnr[k].y );
	}

	x0 = max( 0, (int)floor( xmin ) );
	y0 = max( 0, (int)floor( ymin ) );
	xL = min( ws, (int)ceil( xmax ) );
	yL = min( hs, (int)ceil( ymax ) );
}

/* --------------------------------------------------------------- */
/* Downsample ---------------------------------------------------- */
/* --------------------------------------------------------------- */

static void Downsample( uint8 *ras, int &w, int &h, int iscl )
{
	int	n  = iscl * iscl,
		ws = (int)ceil( (double)w / iscl ),
		hs = (int)ceil( (double)h / iscl ),
		w0 = w,
		xo, yo, xr, yr;

	yr = iscl - h % iscl;
	xr = iscl - w % iscl;

	for( int iy = 0; iy < hs; ++iy ) {

		yo = 0;

		if( iy == hs - 1 )
			yo = yr;

		for( int ix = 0; ix < ws; ++ix ) {

			double	sum = 0.0;

			xo = 0;

			if( ix == ws - 1 )
				xo = xr;

			for( int dy = 0; dy < iscl; ++dy ) {

				for( int dx = 0; dx < iscl; ++dx )
					sum += ras[ix*iscl-xo+dx + w0*(iy*iscl-yo+dy)];
			}

			ras[ix+ws*iy] = int(sum / n);
		}
	}

	w = ws;
	h = hs;
}

/* --------------------------------------------------------------- */
/* NormRas ------------------------------------------------------- */
/* --------------------------------------------------------------- */

// Force image mean to 127 and sd to sdnorm.
//
static void NormRas( uint8 *r, int w, int h, int lgord, int sdnorm )
{
// flatfield & convert to doubles

	int				n = w * h;
	vector<double>	v;

	LegPolyFlatten( v, r, w, h, lgord );

// rescale to mean=127, sd=sdnorm

	for( int i = 0; i < n; ++i ) {

		int	pix = 127 + int(v[i] * sdnorm);

		if( pix < 0 )
			pix = 0;
		else if( pix > 255 )
			pix = 255;

		r[i] = pix;
	}
}

/* --------------------------------------------------------------- */
/* Paint --------------------------------------------------------- */
/* --------------------------------------------------------------- */

static void Paint(
	uint8					*scp,
	uint32					ws,
	uint32					hs,
	const vector<ScpTile>	&vTile,
	int						iscl,
	int						lgord,
	int						sdnorm,
	FILE*					flog )
{
	int		nt = vTile.size();

	for( int i = 0; i < nt; ++i ) {

		TAffine	inv;
		int		x0, xL, y0, yL,
				wL, hL,
				wi, hi;
		uint32	w,  h;
		uint8*	src = Raster8FromAny(
						vTile[i].name.c_str(), w, h, flog );

		if( sdnorm > 0 )
			NormRas( src, w, h, lgord, sdnorm );

		ScanLims( x0, xL, y0, yL, ws, hs, vTile[i].t2g, w, h );
		wi = w;
		hi = h;

		inv.InverseOf( vTile[i].t2g );

		if( iscl > 1 ) {	// Scaling down

			// actually downsample src image
			Downsample( src, wi, hi, iscl );

			// and point at the new pixels
			TAffine	A;
			A.NUSetScl( 1.0/iscl );
			inv = A * inv;
		}

		wL = wi - 1;
		hL = hi - 1;

		for( int iy = y0; iy < yL; ++iy ) {

			for( int ix = x0; ix < xL; ++ix ) {

				Point	p( ix, iy );

				inv.Transform( p );

				if( p.x >= 0 && p.x < wL &&
					p.y >= 0 && p.y < hL ) {

					scp[ix+ws*iy] =
					(int)SafeInterp( p.x, p.y, src, wi, hi );
				}
			}
		}

		RasterFree( src );
	}
}

/* --------------------------------------------------------------- */
/* Scape --------------------------------------------------------- */
/* --------------------------------------------------------------- */

// Allocate and return pointer to new montage built by painting
// the listed tiles, and return its dims (ws, hs), and the top-left
// of the scaled bounding box (x0, y0).
//
// Return NULL if unsuccessful.
//
// (wi, hi)	- specify input tile size (same for all).
// scale	- for example, 0.25 reduces by 4X.
// szmult	- scape dims made divisible by szmult.
// bkval	- default scape value where no data.
// lgord	- Legendre poly max order
// sdnorm	- if > 0, image normalized to mean=127, sd=sdnorm.
//
// Caller must dispose of scape with ImageIO::RasterFree().
//
uint8* Scape(
	uint32			&ws,
	uint32			&hs,
	double			&x0,
	double			&y0,
	vector<ScpTile>	&vTile,
	int				wi,
	int				hi,
	double			scale,
	int				szmult,
	int				bkval,
	int				lgord,
	int				sdnorm,
	FILE*			flog )
{
	if( !vTile.size() ) {
		fprintf( flog, "Scape: Empty tile list.\n" );
		return NULL;
	}

	AdjustBounds( ws, hs, x0, y0, vTile, wi, hi, scale, szmult );

	int		ns		= ws * hs;
	uint8	*scp	= (uint8*)RasterAlloc( ns );

	if( scp ) {
		memset( scp, bkval, ns );
		Paint( scp, ws, hs, vTile, int(1/scale), lgord, sdnorm, flog );
	}
	else
		fprintf( flog, "Scape: Alloc failed (%d x %d).\n", ws, hs );

	return scp;
}


