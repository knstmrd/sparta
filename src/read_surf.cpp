/* ----------------------------------------------------------------------
   DSMC - Sandia parallel DSMC code
   www.sandia.gov/~sjplimp/dsmc.html
   Steve Plimpton, sjplimp@sandia.gov, Michael Gallis, magalli@sandia.gov
   Sandia National Laboratories

   Copyright (2011) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under 
   the GNU General Public License.

   See the README file in the top-level DSMC directory.
------------------------------------------------------------------------- */

#include "mpi.h"
#include "string.h"
#include "stdlib.h"
#include "read_surf.h"
#include "math_extra.h"
#include "surf.h"
#include "domain.h"
#include "grid.h"
#include "error.h"
#include "memory.h"

using namespace DSMC_NS;
using namespace MathExtra;

#define MAXLINE 256
#define CHUNK 1024
#define EPSILON 1.0e-6
#define BIG 1.0e20

/* ---------------------------------------------------------------------- */

ReadSurf::ReadSurf(DSMC *dsmc) : Pointers(dsmc)
{
  MPI_Comm_rank(world,&me);
  line = new char[MAXLINE];
  keyword = new char[MAXLINE];
  buffer = new char[CHUNK*MAXLINE];
}

/* ---------------------------------------------------------------------- */

ReadSurf::~ReadSurf()
{
  delete [] line;
  delete [] keyword;
  delete [] buffer;
}

/* ---------------------------------------------------------------------- */

void ReadSurf::command(int narg, char **arg)
{
  if (!grid->grid_exist) 
    error->all(FLERR,"Cannot read_surf before grid is defined");

  surf->surf_exist = 1;

  if (narg < 2) error->all(FLERR,"Illegal read_surf command");

  dimension = domain->dimension;

  // set surface ID

  id = surf->add_id(arg[0]);

  // read header info

  if (me == 0) {
    if (screen) fprintf(screen,"Reading surf file ...\n");
    open(arg[1]);
  }

  header();

  // extend pts,lines,tris data structures

  pts = surf->pts;
  lines = surf->lines;
  tris = surf->tris;

  npoint_old = surf->npoint;
  nline_old = surf->nline;
  ntri_old = surf->ntri;

  pts = (Surf::Point *) 
    memory->srealloc(pts,(npoint_old+npoint_new)*sizeof(Surf::Point),
		     "surf:pts");
  lines = (Surf::Line *) 
    memory->srealloc(lines,(nline_old+nline_new)*sizeof(Surf::Line),
		     "surf:lines");
  tris = (Surf::Tri *) 
    memory->srealloc(tris,(ntri_old+ntri_new)*sizeof(Surf::Tri),
		     "surf:tris");
  
  // read and store Points and Lines/Tris sections

  parse_keyword(1);
  if (strcmp(keyword,"Points") != 0)
    error->all(FLERR,"Surf file cannot parse Points section");
  read_points();

  parse_keyword(0);
  if (dimension == 2) {
    if (strcmp(keyword,"Lines") != 0)
      error->all(FLERR,"Surf file cannot parse Lines section");
    read_lines();
  } else {
    if (strcmp(keyword,"Triangles") != 0)
    error->all(FLERR,"Surf file cannot parse Triangles section");
    read_tris();
  }

  // close file

  if (me == 0) {
    if (compressed) pclose(fp);
    else fclose(fp);
  }

  // apply optional keywords for geometric transformations

  origin[0] = origin[1] = origin[2] = 0.0;

  int iarg = 2;
  while (iarg < narg) {
    if (strcmp(arg[iarg],"origin") == 0) {
      if (iarg+4 > narg) error->all(FLERR,"Invalid read_surf command");
      double ox = atof(arg[iarg+1]);
      double oy = atof(arg[iarg+2]);
      double oz = atof(arg[iarg+3]);
      if (dimension == 2 && oz != 0.0) 
	error->all(FLERR,"Invalid read_surf geometry transformation "
		   "for 2d simulation");
      origin[0] = ox;
      origin[1] = oy;
      origin[2] = oz;
      iarg += 4;
    } else if (strcmp(arg[iarg],"trans") == 0) {
      if (iarg+4 > narg) error->all(FLERR,"Invalid read_surf command");
      double dx = atof(arg[iarg+1]);
      double dy = atof(arg[iarg+2]);
      double dz = atof(arg[iarg+3]);
      if (dimension == 2 && dz != 0.0) 
	error->all(FLERR,"Invalid read_surf geometry transformation "
		   "for 2d simulation");
      origin[0] += dx;
      origin[1] += dy;
      origin[2] += dz;
      translate(dx,dy,dz);
      iarg += 4;
    } else if (strcmp(arg[iarg],"atrans") == 0) {
      if (iarg+4 > narg) error->all(FLERR,"Invalid read_surf command");
      double ax = atof(arg[iarg+1]);
      double ay = atof(arg[iarg+2]);
      double az = atof(arg[iarg+3]);
      if (dimension == 2 && az != 0.0) 
	error->all(FLERR,"Invalid read_surf geometry transformation "
		   "for 2d simulation");
      double dx = ax - origin[0];
      double dy = ay - origin[1];
      double dz = az - origin[2];
      origin[0] = ax;
      origin[1] = ay;
      origin[2] = az;
      translate(dx,dy,dz);
      iarg += 4;
    } else if (strcmp(arg[iarg],"ftrans") == 0) {
      if (iarg+4 > narg) error->all(FLERR,"Invalid read_surf command");
      double fx = atof(arg[iarg+1]);
      double fy = atof(arg[iarg+2]);
      double fz = atof(arg[iarg+3]);
      if (dimension == 2 && fz != 0.5) 
	error->all(FLERR,"Invalid read_surf geometry transformation "
		   "for 2d simulation");
      double ax = domain->boxlo[0] + fx*domain->xprd;
      double ay = domain->boxlo[1] + fy*domain->yprd;
      double az;
      if (dimension == 3) az = domain->boxlo[2] + fz*domain->zprd;
      else az = 0.0;
      double dx = ax - origin[0];
      double dy = ay - origin[1];
      double dz = az - origin[2];
      origin[0] = ax;
      origin[1] = ay;
      origin[2] = az;
      translate(dx,dy,dz);
      iarg += 4;
    } else if (strcmp(arg[iarg],"scale") == 0) {
      if (iarg+4 > narg) error->all(FLERR,"Invalid read_surf command");
      double sx = atof(arg[iarg+1]);
      double sy = atof(arg[iarg+2]);
      double sz = atof(arg[iarg+3]);
      if (dimension == 2 && sz != 1.0) 
	error->all(FLERR,"Invalid read_surf geometry transformation "
		   "for 2d simulation");
      scale(sx,sy,sz);
      iarg += 4;
    } else if (strcmp(arg[iarg],"rotate") == 0) {
      if (iarg+5 > narg) error->all(FLERR,"Invalid read_surf command");
      double theta = atof(arg[iarg+1]);
      double rx = atof(arg[iarg+2]);
      double ry = atof(arg[iarg+3]);
      double rz = atof(arg[iarg+4]);
      if (dimension == 2 && (rx != 0.0 || ry != 0.0 || rz != 1.0))
	error->all(FLERR,"Invalid read_surf geometry transformation "
		   "for 2d simulation");
      if (rx == 0.0 && ry == 0.0 && rz == 0.0)
	error->all(FLERR,"Invalid read_surf geometry transformation "
		   "for 2d simulation");
      rotate(theta,rx,ry,rz);
      iarg += 5;
    } else if (strcmp(arg[iarg],"invert") == 0) {
      invert();
      iarg += 1;
    } else error->all(FLERR,"Invalid read_surf command");
  }

  // extent of surf after geometric transformations

  double extent[3][2];
  extent[0][0] = extent[1][0] = extent[2][0] = BIG;
  extent[0][1] = extent[1][1] = extent[2][1] = -BIG;

  int m = npoint_old;
  for (int i = 0; i < npoint_new; i++) {
    extent[0][0] = MIN(extent[0][0],pts[m].x[0]);
    extent[0][1] = MAX(extent[0][1],pts[m].x[0]);
    extent[1][0] = MIN(extent[1][0],pts[m].x[1]);
    extent[1][1] = MAX(extent[1][1],pts[m].x[1]);
    extent[2][0] = MIN(extent[2][0],pts[m].x[2]);
    extent[2][1] = MAX(extent[2][1],pts[m].x[2]);
    m++;
  }

  if (me == 0) {
    if (screen) {
      fprintf(screen,"  %g %g xlo xhi\n",extent[0][0],extent[0][1]);
      fprintf(screen,"  %g %g ylo yhi\n",extent[1][0],extent[1][1]);
      fprintf(screen,"  %g %g zlo zhi\n",extent[2][0],extent[2][1]);
    }
    if (logfile) {
      fprintf(logfile,"  %g %g xlo xhi\n",extent[0][0],extent[0][1]);
      fprintf(logfile,"  %g %g ylo yhi\n",extent[1][0],extent[1][1]);
      fprintf(logfile,"  %g %g zlo zhi\n",extent[2][0],extent[2][1]);
    }
  }

  // error checks on new points,lines,tris
  // all points must be strictly inside simulation box
  // no pair of points can be separted by less than EPSILON
  // 2d watertight = every point is part of exactly 2 lines
  // 3d watertight = every edge is part of exactly 2 or 4 triangles

  check_point_inside();
  check_point_pairs();
  if (dimension == 2) check_watertight_2d();
  if (dimension == 3) check_watertight_3d();

  // update Surf data structures

  surf->pts = pts;
  surf->lines = lines;
  surf->tris = tris;

  surf->npoint = npoint_old + npoint_new;
  surf->nline = nline_old + nline_new;
  surf->ntri = ntri_old + ntri_new;

  // compute normals of new lines or triangles

  if (dimension == 2) surf->compute_line_normal(nline_old,nline_new);
  if (dimension == 3) surf->compute_tri_normal(ntri_old,ntri_new);
}

/* ----------------------------------------------------------------------
   read free-format header of data file
   1st line and blank lines are skipped
   non-blank lines are checked for header keywords and leading value is read
   header ends with non-blank line containing no header keyword (or EOF)
   return line with non-blank line (or empty line if EOF)
------------------------------------------------------------------------- */

void ReadSurf::header()
{
  int n;
  char *ptr;

  // skip 1st line of file

  if (me == 0) {
    char *eof = fgets(line,MAXLINE,fp);
    if (eof == NULL) error->one(FLERR,"Unexpected end of data file");
  }

  npoint_new = nline_new = ntri_new = 0;

  while (1) {

    // read a line and bcast length

    if (me == 0) {
      if (fgets(line,MAXLINE,fp) == NULL) n = 0;
      else n = strlen(line) + 1;
    }
    MPI_Bcast(&n,1,MPI_INT,0,world);

    // if n = 0 then end-of-file so return with blank line

    if (n == 0) {
      line[0] = '\0';
      return;
    }

    // bcast line

    MPI_Bcast(line,n,MPI_CHAR,0,world);

    // trim anything from '#' onward
    // if line is blank, continue

    if (ptr = strchr(line,'#')) *ptr = '\0';
    if (strspn(line," \t\n\r") == strlen(line)) continue;

    // search line for header keyword and set corresponding variable

    if (strstr(line,"points")) sscanf(line,"%d",&npoint_new);
    else if (strstr(line,"lines")) {
      if (dimension == 3) 
	error->all(FLERR,"Surf file cannot contain lines for 3d simulation");
      sscanf(line,"%d",&nline_new);
    } else if (strstr(line,"triangles")) {
      if (dimension == 2) 
	error->all(FLERR,
		   "Surf file cannot contain triangles for 2d simulation");
      sscanf(line,"%d",&ntri_new);
    } else break;
  }

  if (npoint_new == 0) error->all(FLERR,"Surf files does not contain points");
  if (dimension == 2 && nline_new == 0) 
    error->all(FLERR,"Surf files does not contain lines");
  if (dimension == 3 && ntri_new == 0) 
    error->all(FLERR,"Surf files does not contain triangles");
}

/* ----------------------------------------------------------------------
   read/store all points
------------------------------------------------------------------------- */

void ReadSurf::read_points()
{
  int i,m,nchunk;
  char *next,*buf;

  // read and broadcast one CHUNK of lines at a time

  int n = npoint_old;
  int nread = 0;
  
  while (nread < npoint_new) {
    if (npoint_new-nread > CHUNK) nchunk = CHUNK;
    else nchunk = npoint_new-nread;
    if (me == 0) {
      char *eof;
      m = 0;
      for (i = 0; i < nchunk; i++) {
	eof = fgets(&buffer[m],MAXLINE,fp);
	if (eof == NULL) error->one(FLERR,"Unexpected end of surf file");
	m += strlen(&buffer[m]);
      }
      m++;
    }
    MPI_Bcast(&m,1,MPI_INT,0,world);
    MPI_Bcast(buffer,m,MPI_CHAR,0,world);

    buf = buffer;
    next = strchr(buf,'\n');
    *next = '\0';
    int nwords = count_words(buf);
    *next = '\n';

    if (dimension == 2 && nwords != 3)
      error->all(FLERR,"Incorrect point format in surf file");
    if (dimension == 3 && nwords != 4)
      error->all(FLERR,"Incorrect point format in surf file");

    for (int i = 0; i < nchunk; i++) {
      next = strchr(buf,'\n');
      strtok(buf," \t\n\r\f");
      pts[n].x[0] = atof(strtok(NULL," \t\n\r\f"));
      pts[n].x[1] = atof(strtok(NULL," \t\n\r\f"));
      if (dimension == 3) pts[n].x[2] = atof(strtok(NULL," \t\n\r\f"));
      else pts[n].x[2] = 0.0;
      n++;
      buf = next + 1;
    }

    nread += nchunk;
  }

  if (me == 0) {
    if (screen) fprintf(screen,"  %d points\n",npoint_new);
    if (logfile) fprintf(logfile,"  %d points\n",npoint_new);
  }
}

/* ----------------------------------------------------------------------
   read/store all lines
------------------------------------------------------------------------- */

void ReadSurf::read_lines()
{
  int i,m,nchunk,p1,p2;
  char *next,*buf;

  // read and broadcast one CHUNK of lines at a time

  int n = nline_old;
  int nread = 0;

  while (nread < nline_new) {
    if (nline_new-nread > CHUNK) nchunk = CHUNK;
    else nchunk = nline_new-nread;
    if (me == 0) {
      char *eof;
      m = 0;
      for (i = 0; i < nchunk; i++) {
	eof = fgets(&buffer[m],MAXLINE,fp);
	if (eof == NULL) error->one(FLERR,"Unexpected end of surf file");
	m += strlen(&buffer[m]);
      }
      m++;
    }
    MPI_Bcast(&m,1,MPI_INT,0,world);
    MPI_Bcast(buffer,m,MPI_CHAR,0,world);

    buf = buffer;
    next = strchr(buf,'\n');
    *next = '\0';
    int nwords = count_words(buf);
    *next = '\n';

    if (nwords != 3)
      error->all(FLERR,"Incorrect line format in surf file");

    for (int i = 0; i < nchunk; i++) {
      next = strchr(buf,'\n');
      strtok(buf," \t\n\r\f");
      p1 = atoi(strtok(NULL," \t\n\r\f"));
      p2 = atoi(strtok(NULL," \t\n\r\f"));
      if (p1 < 1 || p1 > npoint_new || p2 < 1 || p2 > npoint_new || p1 == p2)
	error->all(FLERR,"Invalid point index in line");
      lines[n].id = id;
      lines[n].p1 = p1-1 + npoint_old;
      lines[n].p2 = p2-1 + npoint_old;
      n++;
      buf = next + 1;
    }

    nread += nchunk;
  }

  if (me == 0) {
    if (screen) fprintf(screen,"  %d lines\n",nline_new);
    if (logfile) fprintf(logfile,"  %d lines\n",nline_new);
  }
}

/* ----------------------------------------------------------------------
   read/store all triangles
------------------------------------------------------------------------- */

void ReadSurf::read_tris()
{
  int i,m,nchunk,p1,p2,p3;
  char *next,*buf;

  // read and broadcast one CHUNK of triangles at a time

  int n = ntri_old;
  int nread = 0;

  while (nread < ntri_new) {
    if (ntri_new-nread > CHUNK) nchunk = CHUNK;
    else nchunk = ntri_new-nread;
    if (me == 0) {
      char *eof;
      m = 0;
      for (i = 0; i < nchunk; i++) {
	eof = fgets(&buffer[m],MAXLINE,fp);
	if (eof == NULL) error->one(FLERR,"Unexpected end of surf file");
	m += strlen(&buffer[m]);
      }
      m++;
    }
    MPI_Bcast(&m,1,MPI_INT,0,world);
    MPI_Bcast(buffer,m,MPI_CHAR,0,world);

    buf = buffer;
    next = strchr(buf,'\n');
    *next = '\0';
    int nwords = count_words(buf);
    *next = '\n';

    if (nwords != 4)
      error->all(FLERR,"Incorrect triangle format in surf file");

    for (int i = 0; i < nchunk; i++) {
      next = strchr(buf,'\n');
      strtok(buf," \t\n\r\f");
      p1 = atoi(strtok(NULL," \t\n\r\f"));
      p2 = atoi(strtok(NULL," \t\n\r\f"));
      p3 = atoi(strtok(NULL," \t\n\r\f"));
      if (p1 < 1 || p1 > npoint_new || p2 < 1 || p2 > npoint_new || 
	  p3 < 1 || p3 > npoint_new || p1 == p2 || p2 == p3)
	error->all(FLERR,"Invalid point index in triangle");
      tris[n].id = id;
      tris[n].p1 = p1-1 + npoint_old;
      tris[n].p2 = p2-1 + npoint_old;
      tris[n].p3 = p3-1 + npoint_old;
      n++;
      buf = next + 1;
    }

    nread += nchunk;
  }

  if (me == 0) {
    if (screen) fprintf(screen,"  %d triangles\n",ntri_new);
    if (logfile) fprintf(logfile,"  %d triangles\n",ntri_new);
  }
}

/* ----------------------------------------------------------------------
   translate new vertices by (dx,dy,dz)
   for 2d, dz will be 0.0
------------------------------------------------------------------------- */

void ReadSurf::translate(double dx, double dy, double dz)
{
  int m = npoint_old;
  for (int i = 0; i < npoint_new; i++) {
    pts[m].x[0] += dx;
    pts[m].x[1] += dy;
    pts[m].x[2] += dz;
    m++;
  }
}

/* ----------------------------------------------------------------------
   scale new vertices by (sx,sy,sz) around origin
   for 2d, do not reset x[2] to avoid epsilon change
------------------------------------------------------------------------- */

void ReadSurf::scale(double sx, double sy, double sz)
{
  int m = npoint_old;
  for (int i = 0; i < npoint_new; i++) {
    pts[m].x[0] = sx*(pts[m].x[0]-origin[0]) + origin[0];
    pts[m].x[1] = sy*(pts[m].x[1]-origin[1]) + origin[1];
    if (dimension == 3) pts[m].x[2] = sz*(pts[m].x[2]-origin[2]) + origin[2];
    m++;
  }
}

/* ----------------------------------------------------------------------
   rotate new vertices around origin
   for 2d, do not reset x[2] to avoid epsilon change
------------------------------------------------------------------------- */

void ReadSurf::rotate(double theta, double rx, double ry, double rz)
{
  double r[3],q[4],d[3],dnew[3];
  double rotmat[3][3];

  r[0] = rx; r[1] = ry; r[2] = rz;
  MathExtra::norm3(r);
  MathExtra::axisangle_to_quat(r,theta,q);
  MathExtra::quat_to_mat(q,rotmat);

  int m = npoint_old;
  for (int i = 0; i < npoint_new; i++) {
    d[0] = pts[m].x[0] - origin[0];
    d[1] = pts[m].x[1] - origin[1];
    d[2] = pts[m].x[2] - origin[2];
    MathExtra::matvec(rotmat,d,dnew);
    pts[m].x[0] = dnew[0] + origin[0];
    pts[m].x[1] = dnew[1] + origin[1];
    if (dimension == 3) pts[m].x[2] = dnew[2] + origin[2];
    m++;
  }
}

/* ----------------------------------------------------------------------
   invert new vertex ordering within each line or tri
   this flips direction of surface normal
------------------------------------------------------------------------- */

void ReadSurf::invert()
{
  int tmp;

  if (dimension == 2) {
    int m = nline_old;
    for (int i = 0; i < nline_new; i++) {
      tmp = lines[m].p1;
      lines[m].p1 = lines[m].p2;
      lines[m].p2 = tmp;
      m++;
    }
  }

  if (dimension == 3) {
    int m = ntri_old;
    for (int i = 0; i < nline_new; i++) {
      tmp = tris[m].p2;
      tris[m].p2 = tris[m].p3;
      tris[m].p3 = tmp;
      m++;
    }
  }
}

/* ----------------------------------------------------------------------
   check if all new points are strictly inside global simulation box
------------------------------------------------------------------------- */

void ReadSurf::check_point_inside()
{
  double *x;

  double *boxlo = domain->boxlo;
  double *boxhi = domain->boxhi;

  int m = npoint_old;
  int nbad = 0;
  for (int i = 0; i < npoint_new; i++) {
    x = pts[m].x;
    if (x[0] <= boxlo[0] || x[0] >= boxhi[0] ||
	x[1] <= boxlo[1] || x[1] >= boxhi[1] ||
	x[2] <= boxlo[2] || x[2] >= boxhi[2]) nbad++;
    m++;
  }

  if (nbad) {
    char str[128];
    sprintf(str,"%d read_surf points are not inside simulation box",
	    nbad);
    error->all(FLERR,str);
  }
}

/* ----------------------------------------------------------------------
   check if any pair of points is closer than epsilon
   done in O(N) manner by binning twice with offset bins
------------------------------------------------------------------------- */

void ReadSurf::check_point_pairs()
{
  int i,j,k,m,n,ix,iy,iz;
  double dx,dy,dz,rsq;
  double origin[3];

  double *boxlo = domain->boxlo;
  double *boxhi = domain->boxhi;

  // epsilon = EPSILON fraction of shortest box length
  // epssq = epsilon squared

  double epsilon = MIN(domain->xprd,domain->yprd);
  if (dimension == 3) epsilon = MIN(epsilon,domain->zprd);
  epsilon *= EPSILON;
  double epssq = epsilon * epsilon;

  // goal: N roughly cubic bins where N = # of new points
  // nbinxyz = # of bins in each dimension
  // xyzbin = bin size in each dimension
  // for 2d, nbinz = 1
  // add 1 to nbinxyz to allow for 2nd binning via offset origin

  int nbinx,nbiny,nbinz;
  double xbin,ybin,zbin;
  double xbininv,ybininv,zbininv;
  
  if (dimension == 2) {
    double vol_per_point = domain->xprd * domain->yprd / npoint_new;
    xbin = ybin = sqrt(vol_per_point);
    nbinx = static_cast<int> (domain->xprd / xbin);
    nbiny = static_cast<int> (domain->yprd / ybin);
    if (nbinx == 0) nbinx = 1;
    if (nbiny == 0) nbiny = 1;
    nbinz = 1;
  } else {
    double vol_per_point = domain->xprd * domain->yprd * domain->zprd / 
      npoint_new;
    xbin = ybin = zbin = pow(vol_per_point,1.0/3.0);
    nbinx = static_cast<int> (domain->xprd / xbin);
    nbiny = static_cast<int> (domain->yprd / ybin);
    nbinz = static_cast<int> (domain->zprd / zbin);
    if (nbinx == 0) nbinx = 1;
    if (nbiny == 0) nbiny = 1;
    if (nbinz == 0) nbinz = 1;
  }

  if (nbinx > 1) nbinx++;
  if (nbiny > 1) nbiny++;
  if (nbinz > 1) nbinz++;
  xbin = domain->xprd / nbinx;
  ybin = domain->yprd / nbiny;
  zbin = domain->zprd / nbinz;
  xbininv = 1.0/xbin;
  ybininv = 1.0/ybin;
  zbininv = 1.0/zbin;

  // binhead[I][J][K] = point index of 1st point in bin IJK, -1 if none
  // bin[I] = index of next point in same bin as point I, -1 if last

  int ***binhead;
  memory->create(binhead,nbinx,nbiny,nbinz,"readsurf:binhead");
  int *bin;
  memory->create(bin,npoint_new,"readsurf:bin");

  // 1st binning = bins aligned with global box boundaries

  for (i = 0; i < nbinx; i++)
    for (j = 0; j < nbiny; j++)
      for (k = 0; k < nbinz; k++)
	binhead[i][j][k] = -1;

  origin[0] = boxlo[0];
  origin[1] = boxlo[1];
  origin[2] = boxlo[2];

  m = npoint_old;
  for (i = 0; i < npoint_new; i++) {
    ix = static_cast<int> ((pts[m].x[0] - origin[0]) * xbininv);
    iy = static_cast<int> ((pts[m].x[1] - origin[1]) * ybininv);
    iz = static_cast<int> ((pts[m].x[2] - origin[2]) * zbininv);
    bin[m] = binhead[ix][iy][iz];
    binhead[ix][iy][iz] = m;
    m++;
  }

  // DEBUG

  /*
  int nmax;
  for (i = 0; i < nbinx; i++)
    for (j = 0; j < nbiny; j++)
      for (k = 0; k < nbinz; k++) {
	m = binhead[i][j][k];
	n = 0;
	while (m >= 0) {
	  m = bin[m];
	  n++;
	}
	nmax = MAX(nmax,n);
      }

  printf("MAX points in any pairwise bin = %d\n",nmax);
  */

  // check distances for all pairs of particles in same bin

  int nbad = 0;

  for (i = 0; i < nbinx; i++)
    for (j = 0; j < nbiny; j++)
      for (k = 0; k < nbinz; k++) {
	m = binhead[i][j][k];
	while (m >= 0) {
	  n = bin[m];
	  while (n >= 0) {
	    dx = pts[m].x[0] - pts[n].x[0];
	    dy = pts[m].x[1] - pts[n].x[1];
	    dz = pts[m].x[2] - pts[n].x[2];
	    rsq = dx*dx + dy*dy + dz*dz;
	    if (rsq < epssq) nbad++;
	    n = bin[n];
	  }
	  m = bin[m];
	}
      }

  if (nbad) {
    char str[128];
    sprintf(str,"%d read_surf point pairs are too close",nbad);
    error->all(FLERR,str);
  }

  // 2nd binning = bins offset by 1/2 binsize wrt global box boundaries
  // do not offset bin origin in a dimension with only 1 bin

  for (i = 0; i < nbinx; i++)
    for (j = 0; j < nbiny; j++)
      for (k = 0; k < nbinz; k++)
	binhead[i][j][k] = -1;

  origin[0] = boxlo[0] - 0.5*xbin;
  origin[1] = boxlo[1] - 0.5*ybin;
  origin[2] = boxlo[2] - 0.5*zbin;
  if (nbinx == 1) origin[0] = boxlo[0];
  if (nbiny == 1) origin[1] = boxlo[1];
  if (nbinz == 1) origin[2] = boxlo[2];

  m = npoint_old;
  for (i = 0; i < npoint_new; i++) {
    ix = static_cast<int> ((pts[m].x[0] - origin[0]) * xbininv);
    iy = static_cast<int> ((pts[m].x[1] - origin[1]) * ybininv);
    iz = static_cast<int> ((pts[m].x[2] - origin[2]) * zbininv);
    bin[m] = binhead[ix][iy][iz];
    binhead[ix][iy][iz] = m;
    m++;
  }

  // check distances for all pairs of particles in same bin

  nbad = 0;

  for (i = 0; i < nbinx; i++)
    for (j = 0; j < nbiny; j++)
      for (k = 0; k < nbinz; k++) {
	m = binhead[i][j][k];
	while (m >= 0) {
	  n = bin[m];
	  while (n >= 0) {
	    dx = pts[m].x[0] - pts[n].x[0];
	    dy = pts[m].x[1] - pts[n].x[1];
	    dz = pts[m].x[2] - pts[n].x[2];
	    rsq = dx*dx + dy*dy + dz*dz;
	    if (rsq < epssq) nbad++;
	    n = bin[n];
	  }
	  m = bin[m];
	}
      }

  if (nbad) {
    char str[128];
    sprintf(str,"%d read_surf point pairs are too close",nbad);
    error->all(FLERR,str);
  }

  // clean up

  memory->destroy(binhead);
  memory->destroy(bin);
}

/* ----------------------------------------------------------------------
   check if every new point is an end point of exactly 2 new line segments
------------------------------------------------------------------------- */

void ReadSurf::check_watertight_2d()
{
  int p1,p2;

  // count[I] = # of lines that vertex I is part of

  int *count;
  memory->create(count,npoint_new,"readsurf:count");
  for (int i = 0; i < npoint_new; i++) count[i] = 0;

  int m = nline_old;
  for (int i = 0; i < nline_new; i++) {
    p1 = lines[m].p1 - npoint_old;
    p2 = lines[m].p2 - npoint_old;
    count[p1]++;
    count[p2]++;
    m++;
  }
  
  // check that all counts are 2

  int nbad = 0;
  for (int i = 0; i < npoint_new; i++)
    if (count[i] != 2) nbad++;

  // clean up

  memory->destroy(count);

  // error message

  if (nbad) {
    char str[128];
    sprintf(str,"%d read_surf lines are not watertight",nbad);
    error->all(FLERR,str);
  }
}

/* ----------------------------------------------------------------------
   check if every new triangle edge is part of exactly 2 or 4 new triangles
   4 triangles with 1 common edge can occur with infinitely thin surface
------------------------------------------------------------------------- */

void ReadSurf::check_watertight_3d()
{
  int i,j,m,p1,p2,p3,pi,pj;

  // ecountmax[I] = max # of edges that vertex I is part of
  // use lowest vertex in each edge
  // divide by 2 to give upper bound, assuming edge will appear 2 or 4 times

  int *ecountmax;
  memory->create(ecountmax,npoint_new,"readsurf:ecountmax");
  for (i = 0; i < npoint_new; i++) ecountmax[i] = 0;

  m = ntri_old;
  for (i = 0; i < ntri_new; i++) {
    p1 = tris[m].p1 - npoint_old;
    p2 = tris[m].p2 - npoint_old;
    p3 = tris[m].p3 - npoint_old;
    ecountmax[MIN(p1,p2)]++;
    ecountmax[MIN(p2,p3)]++;
    ecountmax[MIN(p3,p1)]++;
    m++;
  }

  for (i = 0; i < npoint_new; i++) ecountmax[i] /= 2;

  // DEBUG

  /*
  printf("ECMAX %d %d %d %d\n",
	 ecountmax[0],ecountmax[1],ecountmax[2],ecountmax[3]);
  */

  // ecount[I] = # of edges that vertex I is part of
  // edge[I][J] = Jth vertex connected to vertex I via an edge
  // count[I][J] = # of times edge IJ appears in surf of triangles
  // edge & count allocated as ragged 2d arrays using ecountmax for 2nd dim
  // insure ecount < ecountmax

  int *ecount;
  memory->create(ecount,npoint_new,"readsurf:ecount");
  int **edge;
  memory->create_ragged(edge,npoint_new,ecountmax,"readsurf:edge");
  int **count;
  memory->create_ragged(count,npoint_new,ecountmax,"readsurf:count");

  for (i = 0; i < npoint_new; i++) ecount[i] = 0;

  int nbad1 = 0;
  m = ntri_old;
  for (i = 0; i < ntri_new; i++) {
    p1 = tris[m].p1 - npoint_old;
    p2 = tris[m].p2 - npoint_old;
    p3 = tris[m].p3 - npoint_old;
    
    pi = MIN(p1,p2);
    pj = MAX(p1,p2);
    for (j = 0; j < ecount[pi]; j++)
      if (edge[pi][j] == pj) break;
    if (j == ecount[pi]) {
      if (ecount[pi] == ecountmax[pi]) nbad1++;
      else {
	edge[pi][j] = pj;
	count[pi][j] = 1;
	ecount[pi]++;
      }
    } else count[pi][j]++;

    pi = MIN(p2,p3);
    pj = MAX(p2,p3);
    for (j = 0; j < ecount[pi]; j++)
      if (edge[pi][j] == pj) break;
    if (j == ecount[pi]) {
      if (ecount[pi] == ecountmax[pi]) nbad1++;
      else {
	edge[pi][j] = pj;
	count[pi][j] = 1;
	ecount[pi]++;
      }
    } else count[pi][j]++;

    pi = MIN(p3,p1);
    pj = MAX(p3,p1);
    for (j = 0; j < ecount[pi]; j++)
      if (edge[pi][j] == pj) break;
    if (j == ecount[pi]) {
      if (ecount[pi] == ecountmax[pi]) nbad1++;
      else {
	edge[pi][j] = pj;
	count[pi][j] = 1;
	ecount[pi]++;
      }
    } else count[pi][j]++;

    m++;
  }

  // DEBUG

  /*
  printf("NBAD1 %d\n",nbad1);

    printf("EC %d %d %d %d\n",
	   ecount[0],ecount[1],ecount[2],ecount[3]);

    for (int ii = 0; ii < npoint_new; ii++) {
      printf("PT %d:",ii+1);
      for (j = 0; j < ecount[ii]; j++)
	printf(" %d",edge[ii][j]+1);
      printf("\n");
    }
    
    for (int ii = 0; ii < npoint_new; ii++) {
      printf("COUNT %d:",ii+1);
      for (j = 0; j < ecount[ii]; j++)
	printf(" %d",count[ii][j]);
      printf("\n");
    }
    
    printf("DONE %d\n",i+1);
  */

  // check that all counts are 2 or 4

  int nbad2 = 0;
  for (i = 0; i < npoint_new; i++)
    for (j = 0; j < ecount[i]; j++)
      if (count[i][j] != 2 && count[i][j] != 4) nbad2++;

  // clean up

  memory->destroy(ecountmax);
  memory->destroy(ecount);
  memory->destroy(edge);
  memory->destroy(count);

  // error messages

  int nbad = nbad1 + nbad2;
  if (nbad) {
    char str[128];
    sprintf(str,"%d read_surf triangle edges are not watertight",nbad);
    error->all(FLERR,str);
  }
}

/* ----------------------------------------------------------------------
   proc 0 opens data file
   test if gzipped
------------------------------------------------------------------------- */

void ReadSurf::open(char *file)
{
  compressed = 0;
  char *suffix = file + strlen(file) - 3;
  if (suffix > file && strcmp(suffix,".gz") == 0) compressed = 1;
  if (!compressed) fp = fopen(file,"r");
  else {
#ifdef LAMMPS_GZIP
    char gunzip[128];
    sprintf(gunzip,"gunzip -c %s",file);
    fp = popen(gunzip,"r");
#else
    error->one(FLERR,"Cannot open gzipped file");
#endif
  }

  if (fp == NULL) {
    char str[128];
    sprintf(str,"Cannot open file %s",file);
    error->one(FLERR,str);
  }
}

/* ----------------------------------------------------------------------
   grab next keyword
   read lines until one is non-blank
   keyword is all text on line w/out leading & trailing white space
   read one additional line (assumed blank)
   if any read hits EOF, set keyword to empty
   if first = 1, line variable holds non-blank line that ended header
------------------------------------------------------------------------- */

void ReadSurf::parse_keyword(int first)
{
  int eof = 0;

  // proc 0 reads upto non-blank line plus 1 following line
  // eof is set to 1 if any read hits end-of-file

  if (me == 0) {
    if (!first) {
      if (fgets(line,MAXLINE,fp) == NULL) eof = 1;
    }
    while (eof == 0 && strspn(line," \t\n\r") == strlen(line)) {
      if (fgets(line,MAXLINE,fp) == NULL) eof = 1;
    }
    if (fgets(buffer,MAXLINE,fp) == NULL) eof = 1;
  }

  // if eof, set keyword empty and return

  MPI_Bcast(&eof,1,MPI_INT,0,world);
  if (eof) {
    keyword[0] = '\0';
    return;
  }

  // bcast keyword line to all procs

  int n;
  if (me == 0) n = strlen(line) + 1;
  MPI_Bcast(&n,1,MPI_INT,0,world);
  MPI_Bcast(line,n,MPI_CHAR,0,world);

  // copy non-whitespace portion of line into keyword

  int start = strspn(line," \t\n\r");
  int stop = strlen(line) - 1;
  while (line[stop] == ' ' || line[stop] == '\t' 
	 || line[stop] == '\n' || line[stop] == '\r') stop--;
  line[stop+1] = '\0';
  strcpy(keyword,&line[start]);
}

/* ----------------------------------------------------------------------
   count and return words in a single line
   make copy of line before using strtok so as not to change line
   trim anything from '#' onward
------------------------------------------------------------------------- */

int ReadSurf::count_words(char *line)
{
  int n = strlen(line) + 1;
  char *copy = (char *) memory->smalloc(n*sizeof(char),"copy");
  strcpy(copy,line);

  char *ptr;
  if (ptr = strchr(copy,'#')) *ptr = '\0';

  if (strtok(copy," \t\n\r\f") == NULL) {
    memory->sfree(copy);
    return 0;
  }
  n = 1;
  while (strtok(NULL," \t\n\r\f")) n++;

  memory->sfree(copy);
  return n;
}