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
#define DELTA 4

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

  Surf::Point *pts = surf->pts;
  Surf::Line *lines = surf->lines;
  Surf::Tri *tris = surf->tris;

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

  // error checks on new points,lines,tris
  // all points inside simulation box
  // no pair of points can be separted by less than EPSILON
  // 2d watertight = every point is part of exactly 2 lines
  // 3d watertight = every edge is part of exactly 2 triangles

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
    } else if (strstr(line,"tris")) {
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
      lines[n].p1 = p1;
      lines[n].p2 = p2;
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
      tris[n].p1 = p1;
      tris[n].p2 = p2;
      tris[n].p3 = p3;
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
   translate new vertices by (dx,dy,dz)
   for 2d, dz will be 0.0
------------------------------------------------------------------------- */

void ReadSurf::translate(double dx, double dy, double dz)
{
  Surf::Point *pts = surf->pts;
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
  Surf::Point *pts = surf->pts;
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

  Surf::Point *pts = surf->pts;
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
    Surf::Line *lines = surf->lines;
    int m = nline_old;

    for (int i = 0; i < nline_new; i++) {
      tmp = lines[m].p1;
      lines[m].p1 = lines[m].p2;
      lines[m].p2 = tmp;
      m++;
    }
  }

  if (dimension == 3) {
    Surf::Tri *tris = surf->tris;
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