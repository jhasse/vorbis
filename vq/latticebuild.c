/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE Ogg Vorbis SOFTWARE CODEC SOURCE CODE.  *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS SOURCE IS GOVERNED BY *
 * THE GNU PUBLIC LICENSE 2, WHICH IS INCLUDED WITH THIS SOURCE.    *
 * PLEASE READ THESE TERMS DISTRIBUTING.                            *
 *                                                                  *
 * THE OggSQUISH SOURCE CODE IS (C) COPYRIGHT 1994-2000             *
 * by Monty <monty@xiph.org> and The XIPHOPHORUS Company            *
 * http://www.xiph.org/                                             *
 *                                                                  *
 ********************************************************************

 function: utility main for building codebooks from lattice descriptions
 last mod: $Id: latticebuild.c,v 1.1.2.1 2000/04/21 16:35:40 xiphmont Exp $

 ********************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include "vorbis/codebook.h"
#include "../lib/sharedbook.h"
#include "bookutil.h"

/* The purpose of this util is actually just to count hits and finish
   packaging the description into a static codebook.

   command line:
   vqlattice description.vql datafile.vqd 

   the lattice description file contains four lines:

   <n> <dim> <multiplicitavep>
   <value_0> <value_1> <value_2> ... <value_n-1>
   <thresh_0> <thresh_1> <thresh_2> ... <thresh_n-2>
   <map_0> <map_1> <map_2> ... <map_n-1>

   vqlattice sends residual data (for the next stage) to stdout, and
   produces description.vqh */

int main(int argc,char *argv[]){
  codebook b;
  static_codebook c;
  encode_aux_threshmatch t;
  double *quantlist;
  long *hits;

  int entries=-1,dim=-1,quantvals=-1,addmul=-1;
  FILE *out=NULL;
  FILE *in=NULL;
  char *line,*name;
  long j,k;

  memset(&b,0,sizeof(b));
  memset(&c,0,sizeof(c));
  memset(&t,0,sizeof(t));

  if(argv[1]==NULL){
    fprintf(stderr,"Need a lattice description file on the command line.\n");
    exit(1);
  }

  {
    char *ptr;
    char *filename=calloc(strlen(argv[1])+4,1);

    strcpy(filename,argv[1]);
    in=fopen(filename,"r");
    if(!in){
      fprintf(stderr,"Could not open input file %s\n",filename);
      exit(1);
    }
    
    ptr=strrchr(filename,'.');
    if(ptr){
      *ptr='\0';
      name=strdup(filename);
      sprintf(ptr,".vqh");
    }else{
      name=strdup(filename);
      strcat(filename,".vqh");
    }

    out=fopen(filename,"w");
    if(out==NULL){
      fprintf(stderr,"Unable to open %s for writing\n",filename);
      exit(1);
    }
  }
  
  /* read the description */
  line=get_line(in);
  if(sscanf(line,"%d %d %d",&quantvals,&dim,&addmul)!=3){
    fprintf(stderr,"Syntax error reading book file (line 1)\n");
    exit(1);
  }
  entries=pow(quantvals,dim);
  c.thresh_tree=&t;
  c.dim=dim;
  c.entries=entries;
  c.lengthlist=calloc(entries,sizeof(long));
  c.maptype=1;
  c.q_sequencep=0;
  c.quantlist=calloc(quantvals,sizeof(long));
  t.quantthresh=calloc(quantvals-1,sizeof(double));
  t.quantmap=calloc(quantvals,sizeof(int));
  t.quantvals=quantvals;

  quantlist=malloc(sizeof(long)*c.dim*c.entries);
  hits=malloc(c.entries*sizeof(long));
  for(j=0;j<entries;j++)hits[j]=1;

  reset_next_value();
  setup_line(in);
  for(j=0;j<quantvals;j++){  
    if(get_line_value(in,quantlist+j)==-1){
      fprintf(stderr,"Ran out of data on line 2 of description file\n");
      exit(1);
    }
  }

  setup_line(in);
  for(j=0;j<quantvals-1;j++){  
    if(get_line_value(in,t.quantthresh+j)==-1){
      fprintf(stderr,"Ran out of data on line 3 of description file\n");
      exit(1);
    }
  }

  setup_line(in);
  for(j=0;j<quantvals;j++){  
    if(get_next_ivalue(in,t.quantmap+j)==-1){
      fprintf(stderr,"Ran out of data on line 4 of description file\n");
      exit(1);
    }
  }

  /* gen a real quant list from the more easily human-grokked input */
  {
    double min=quantlist[0];
    double mindel=fabs(quantlist[0]-quantlist[1]);
    for(j=1;j<quantvals;j++){  
      if(quantlist[j]<min)min=quantlist[j];
      for(k=0;k<j;k++){
	double del=fabs(quantlist[j]-quantlist[k]);
	if(del<mindel)mindel=del;
      }
    }
    c.q_min=_float32_pack(min);
    c.q_delta=_float32_pack(mindel);

    min=_float32_unpack(c.q_min);
    mindel=_float32_unpack(c.q_delta);
    for(j=0;j<quantvals;j++)
      c.quantlist[j]=rint((quantlist[j]-min)/mindel);
  }
  
  vorbis_book_init_encode(&b,&c);
  fclose(in);

  /* at this point, we have enough to do best entry matching.  We
     don't have counts yet, but that's OK; match, write residual data,
     and track hits.  The build word lengths when we're done */

  /* we have to do our own de-interleave */
  if(argv[2]==NULL){
    fprintf(stderr,"Need a data file on the command line.\n");
    exit(1);
  }
  in=fopen(argv[2],"r");
  if(!in){
    fprintf(stderr,"Could not open input file %s\n",argv[2]);
    exit(1);
  }

  {
    int cols=0;
    long lines=0;
    double *vec;
    int interleaved;
    line=setup_line(in);
    /* count cols before we start reading */
    {
      char *temp=line;
      while(*temp==' ')temp++;
      for(cols=0;*temp;cols++){
	while(*temp>32)temp++;
	while(*temp==' ')temp++;
      }
    }
    vec=alloca(cols*sizeof(double));
    interleaved=cols/dim;

    while(line){
      lines++;

      if(!(lines&0xff))spinnit("lines so far...",lines);

      /* don't need to deinterleave; besterror does that for us */
      for(j=0;j<cols;j++)
	if(get_line_value(in,vec+j)){
	  fprintf(stderr,"Too few columns on line %ld in data file\n",lines);
	  exit(1);
	}

      /* process */
      for(j=0;j<interleaved;j++)
	hits[vorbis_book_besterror(&b,vec+j,interleaved,addmul)]++;
	
      /* write */
      for(j=0;j<cols;j++)
	fprintf(stdout,"%g, ",vec[j]);
      fprintf(stdout,"\n");

      line=setup_line(in);
    }
  }
  fclose(in);

  /* build the codeword lengths */

  build_tree_from_lengths(entries,hits,c.lengthlist);

  /* save the book in C header form */
  fprintf(out,
 "/********************************************************************\n"
 " *                                                                  *\n"
 " * THIS FILE IS PART OF THE Ogg Vorbis SOFTWARE CODEC SOURCE CODE.  *\n"
 " * USE, DISTRIBUTION AND REPRODUCTION OF THIS SOURCE IS GOVERNED BY *\n"
 " * THE GNU PUBLIC LICENSE 2, WHICH IS INCLUDED WITH THIS SOURCE.    *\n"
 " * PLEASE READ THESE TERMS DISTRIBUTING.                            *\n"
 " *                                                                  *\n"
 " * THE OggSQUISH SOURCE CODE IS (C) COPYRIGHT 1994-1999             *\n"
 " * by 1999 Monty <monty@xiph.org> and The XIPHOPHORUS Company       *\n"
 " * http://www.xiph.org/                                             *\n"
 " *                                                                  *\n"
 " ********************************************************************\n"
 "\n"
 " function: static codebook autogenerated by vq/latticebuild\n"
 "\n"
 " ********************************************************************/\n\n");

  fprintf(out,"#ifndef _V_%s_VQH_\n#define _V_%s_VQH_\n",name,name);
  fprintf(out,"#include \"vorbis/codebook.h\"\n\n");

  /* first, the static vectors, then the book structure to tie it together. */
  /* quantlist */
  fprintf(out,"static long _vq_quantlist_%s[] = {\n",name);
  for(j=0;j<_book_maptype1_quantvals(&c);j++){
    fprintf(out,"\t%ld,\n",c.quantlist[j]);
  }
  fprintf(out,"};\n\n");

  /* lengthlist */
  fprintf(out,"static long _vq_lengthlist_%s[] = {\n",name);
  for(j=0;j<c.entries;){
    fprintf(out,"\t");
    for(k=0;k<16 && j<c.entries;k++,j++)
      fprintf(out,"%2ld,",c.lengthlist[j]);
    fprintf(out,"\n");
  }
  fprintf(out,"};\n\n");

  /* quantthresh */
  fprintf(out,"static double _vq_quantthresh_%s[] = {\n",name);
  for(j=0;j<t.quantvals-1;){
    fprintf(out,"\t");
    for(k=0;k<8 && j<t.quantvals-1;k++,j++)
      fprintf(out,"%05g,",t.quantthresh[j]);
    fprintf(out,"\n");
  }
  fprintf(out,"};\n\n");

  /* quantmap */
  fprintf(out,"static long _vq_quantmap_%s[] = {\n",name);
  for(j=0;j<t.quantvals;){
    fprintf(out,"\t");
    for(k=0;k<8 && j<t.quantvals;k++,j++)
      fprintf(out,"%5ld,",t.quantmap[j]);
    fprintf(out,"\n");
  }
  fprintf(out,"};\n\n");  

  /* tie it all together */
  
  fprintf(out,"static encode_aux_threshmatch _vq_aux_%s = {\n",name);
  fprintf(out,"\t_vq_quantthresh_%s,\n",name);
  fprintf(out,"\t_vq_quantmap_%s,\n",name);
  fprintf(out,"\t%d\n};\n\n",t.quantvals);
  
  fprintf(out,"static static_codebook _vq_book_%s = {\n",name);
  
  fprintf(out,"\t%ld, %ld,\n",c.dim,c.entries);
  fprintf(out,"\t_vq_lengthlist_%s,\n",name);
  fprintf(out,"\t2, %ld, %ld, %d, %d,\n",
	  c.q_min,c.q_delta,c.q_quant,c.q_sequencep);
  fprintf(out,"\t_vq_quantlist_%s,\n",name);
  fprintf(out,"\tNULL,\n");
  fprintf(out,"\t&_vq_aux_%s\n",name);
  fprintf(out,"};\n\n");

  fprintf(out,"\n#endif\n");
  fclose(out);

  fprintf(stderr,"\r                                                     "
	  "\nDone.\n");
  exit(0);
}