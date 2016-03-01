/* Copyright (c) Colorado School of Mines, 2011.*/
/* All rights reserved.                       */

/* SUKDMDCR: $Revision: 1.3 $ ; $Date: 2011/11/16 17:45:18 $	*/

#include "csException.h"
#include "csSUTraceManager.h"
#include "csSUArguments.h"
#include "csSUGetPars.h"
#include "su_complex_declarations.h"
#include "cseis_sulib.h"
#include <string>

extern "C" {
  #include <pthread.h>
}
#include "su.h"
#include "segy.h"
#include "header.h"

/*********************** self documentation **********************/
std::string sdoc_sukdmdcr =
" 									"
"  SUKDMDCR - 2.5D datuming of receivers for prestack, common source    "
"            data using constant-background data mapping formula.       "
"            (See selfdoc for specific survey requirements.)            "
" 									"
"    sukdmdcr  infile=  outfile=  [parameters] 	         		"
"									"
" Required file parameters:						"
" infile=stdin		file for input seismic traces			"
" outfile=stdout	file for output          			"
" ttfile		file for input traveltime tables		"
"                                                                       "
" Required parameters describing the traveltime tables:		        "
" fzt 			first depth sample in traveltime table		"
" nzt 			number of depth samples in traveltime table	"
" dzt			depth interval in traveltime table		"
" fxt			first lateral sample in traveltime table	"
" nxt			number of lateral samples in traveltime table	"
" dxt			lateral interval in traveltime table		"
" fs 			x-coordinate of first source in table		"
" ns 			number of sources in table			"
" ds 			x-coordinate increment of sources in table	"
"									"
" Parameters describing the input data:                                 "
" nxso                  number of shots                                 "
" dxso                  shot interval                                   "
" fxso=0                x-coordinate of first shot                      "
" nxgo                  number of receiver offsets per shot             "
" dxgo                  receiver offset interval                        "
" fxgo=0                first receiver offset                           "
" dt= or from header (dt)       time sampling interval of input data    "
" ft= or from header (ft)       first time sample of input data         "
"                                                                       "
" Parameters describing the domain of the problem:             		"
" dzo=0.2*dzt		vertical spacing in surface determination       "
" offmax=99999		maximum absolute offset allowed          	"
"                                                                       "
" Parameters describing the recording and datuming surfaces:            "
" recsurf=0             recording surface (horizontal=0, topographic=1) "
" zrec                  defines recording surface when recsurf=0        "
" recfile=              defines recording surface when recsurf=1        "
" datsurf=0             datuming surface (horizontal=0, irregular=1)    "
" zdat                  defines datuming surface when datsurf=0         "
" datfile=              defines datuming surface when datsurf=1         "
"                                                                       "
" Optional parameters describing the extrapolation:                     "
" aperx=nxt*dxt/2  	lateral half-aperture 				"
" v0=1500(m/s)		reference wavespeed               		"
" freq=50               dominant frequency in data, used to determine   "
"                       the minimum distance below the datum that       "
"                       the stationary phase calculation is valid.      "
" scale=1.0             user defined scale factor for output            "
" jpfile=stderr		job print file name 				"
" mtr=100  		print verbal information at every mtr traces	"
" ntr=100000		maximum number of input traces to be datumed	"
"									"
"									"
;

/*  Sun Feb 24 13:30:07 2013
  Automatically modified for usage in SeaSeis  */
namespace sukdmdcr {

/*
 * Computational Notes:                                                
 *   
 * 1. Input traces must be SU format and organized in common shot gathers.
 *
 * 2. Traveltime tables were generated by program rayt2d (or equivalent)     
 *    on any grid, with dimension ns*nxt*nzt. In the extrapolation process,       
 *    traveltimes are interpolated into shot/geophone locations and     
 *    output grids.                                          
 *
 * 3. If the offset value of an input trace is not in the offset array     
 *    of output, the nearest one in the array is chosen.                   
 *
 * 4. Amplitudes are computed using the constant reference wavepeed v0.
 *                                
 * 5. Input traces must specify source and receiver positions via the header  
 *    fields tr.sx and tr.gx.             
 *
 * 6. Recording and datuming surfaces are defined as follows:  If recording
 *    surface is horizontal, set recsurf=0 (default).  Then, zrec will be a
 *    required parameter, set to the depth of surface.  If the recording  
 *    surface is topographic, then set recsurf=1.  Then, recfile is a required
 *    input file.  The input file recfile should be a single column ascii file
 *    with the depth of the recording surface at every surface location (first 
 *    source to last offset), with spacing equal to dxgo. 
 * 
 *    The same holds for the datuming surface, using datsurf, zdat, and datfile.
 *
 *
 * Assumptions and limitations:
 *
 * 1. This code implements a 2.5D extraplolation operator that allows to
 *    transfer data from one reference surface to another.  The formula used in
 *    this application is an adaptation of Bleistein & Jaramillo's 2.5D data
 *    mapping formula for receiver extrapolation.  This is the result of a
 *    stationary phase analysis of the data mapping equation in the case of
 *    a constant source location (shot gather). 
 * 
 *
 * Credits:
 * 
 * Authors:  Steven D. Sheaffer (CWP), 11/97 
 *
 *
 * References:  Sheaffer, S., 1999, "2.5D Downward Continuation of the Seismic
 *              Wavefield Using Kirchhoff Data Mapping."  M.Sc. Thesis, 
 *              Dept. of Mathematical & Computer Sciences, 
 *              Colorado School of Mines.
 *
 *
 */
/**************** end self doc ***********************************/
/* Prototype */
  void sum2(int nx,int nz,float a1,float a2,float **t1,float **t2,float **t);
  void dat2d(float *trace,int nt,float ft,float dt,float sx,float gx,
	     float **dat,float aperx,int nxgo,float fxin,float dxgo,float nzo,
  	     float fzo,float dzo,int nxi,float fxi,
             float dxi,float **tsum,int sgn,
             int nzt,float fzt,float dzt,int nxt,float fxt,float dxt,
             float **szif,float v0,float freq);
  void filt(float *trace,int nt,float dt,int sgn);


/* segy trace */
segy tr, tro;

void* main_sukdmdcr( void* args )
{
	int   nt,nzt,nxt,nzo,nxso,nxgo,ns,is,io,ixo,it,
	      ntr,jtr,ktr,mtr,nxi,*ng,sgn,ngl;
	long  nseek;
	float ft,fzt,fxt,fzo,fxso,fxgo,fs,dt,dzt,dxt,dzo,dxso,dxgo,scale,
	      ds,ext,ezt,ezo,exso,exgo,es,s,scal,fxin,
              as,res,freq;
	float v0,offmax,aperx,sx,gx,dxi,fxi,fgl;
	float ***dats,***ttab,**tsum,**szif;
	
	char  *datain="stdin",*dataout="stdout",*ttfile,*jpfile;
	FILE  *infp,*outfp,*ttfp,*jpfp,*hdrfp;

        int recsurf,datsurf,i;
        float zrec,zdat,r1,r2;

        char  *recfile,*datfile;
        FILE  *recfp=NULL,*datfp=NULL;


        /* explicitly define downward continuation */
        sgn=1;


	/* hook up getpar to handle the parameters */
	cseis_su::csSUArguments* suArgs = (cseis_su::csSUArguments*)args;
	cseis_su::csSUTraceManager* cs2su = suArgs->cs2su;
	cseis_su::csSUTraceManager* su2cs = suArgs->su2cs;
	int argc = suArgs->argc;
	char **argv = suArgs->argv;
	cseis_su::csSUGetPars parObj;

	void* retPtr = NULL;  /*  Dummy pointer for return statement  */
	su2cs->setSUDoc( sdoc_sukdmdcr );
	if( su2cs->isDocRequestOnly() ) return retPtr;
	parObj.initargs(argc, argv);

	try {  /* Try-catch block encompassing the main function body */


	/* open input and output files	*/
	if( !parObj.getparstring("datain",&datain)) {
		infp = stdin;
	} else  
		if ((infp=fopen(datain,"r"))==NULL)
			throw cseis_geolib::csException("cannot open datain=%s\n",datain);
	if( !parObj.getparstring("dataout",&dataout)) {
		outfp = stdout;
	} else  
		outfp = fopen(dataout,"w");
	fseek(infp,0,1);
	fseek(outfp,0,1);

	if( !parObj.getparstring("ttfile",&ttfile))
		throw cseis_geolib::csException("must specify ttfile!\n");
	if ((ttfp=fopen(ttfile,"r"))==NULL)
		throw cseis_geolib::csException("cannot open ttfile=%s\n",ttfile);

	if( !parObj.getparstring("jpfile",&jpfile)) {
		jpfp = stderr;
	} else  
		jpfp = fopen(jpfile,"w");


	/* get information from the first header */
	if (!fgettr(infp,&tr)) throw cseis_geolib::csException("can't get first trace");
	nt = tr.ns;
	if (!parObj.getparfloat("dt",&dt)) dt = tr.dt/1000000.0; 
	if (dt<0.0000001) throw cseis_geolib::csException("dt must be positive!\n");
	if (!parObj.getparfloat("ft",&ft)) ft = (float)tr.delrt/1000.; 
	
	/* get traveltime table parameters	*/
	if (!parObj.getparint("nxt",&nxt)) throw cseis_geolib::csException("must specify nxt!\n");
	if (!parObj.getparfloat("fxt",&fxt)) throw cseis_geolib::csException("must specify fxt!\n");
	if (!parObj.getparfloat("dxt",&dxt)) throw cseis_geolib::csException("must specify dxt!\n");
	if (!parObj.getparint("nzt",&nzt)) throw cseis_geolib::csException("must specify nzt!\n");
	if (!parObj.getparfloat("fzt",&fzt)) throw cseis_geolib::csException("must specify fzt!\n");
	if (!parObj.getparfloat("dzt",&dzt)) throw cseis_geolib::csException("must specify dzt!\n");
	if (!parObj.getparint("ns",&ns)) throw cseis_geolib::csException("must specify ns!\n");
	if (!parObj.getparfloat("fs",&fs)) throw cseis_geolib::csException("must specify fs!\n");
	if (!parObj.getparfloat("ds",&ds)) throw cseis_geolib::csException("must specify ds!\n");
	ext = fxt+(nxt-1)*dxt;
	ezt = fzt+(nzt-1)*dzt;
	es = fs+(ns-1)*ds;

	/* optional parameters	*/
	if (!parObj.getparint("nxso",&nxso)) throw cseis_geolib::csException("must specify nxso!\n");
	if (!parObj.getparfloat("fxso",&fxso)) fxso = 0;
	if (!parObj.getparfloat("dxso",&dxso)) throw cseis_geolib::csException("must specify dxso!\n");
        if (!parObj.getparint("nxgo",&nxgo)) throw cseis_geolib::csException("must specify nxgo!\n");
        if (!parObj.getparfloat("fxgo",&fxgo)) fxgo = 0; 
        if (!parObj.getparfloat("dxgo",&dxgo)) throw cseis_geolib::csException("must specify dxgo!\n");

        /* errors for illegal survey geometry */        
        if (nxso>1 && dxgo != dxso) throw cseis_geolib::csException("in this implementation, dxso must = dxgo!\n");
        if (dxgo < 0.) throw cseis_geolib::csException("in this implementation, dxgo must be >0!\n");
        if (dxso < 0.) throw cseis_geolib::csException("in this implementation, dxso must be >0!\n");

        /* calculate the number of independent locations spanning the survey. */
        dxi = dxgo;
        fxi = fxgo;
        nxi = (int)((fxgo + (nxgo+nxso-2)*dxgo)/dxi) + 1;

        /* calculate the number of independent receiver locations or number of  */
        /* receiver gathers.      */                                               
        fgl = fxso + fxgo;
        ngl = nxgo + (nxso-1); 
 
        /* location of last source and receiver */
        exso = fxso+(nxso-1)*dxso;
        exgo = fxgo+(nxi-1)*dxgo;


        /* get the depth resolution for interpolation */
	if (!parObj.getparfloat("dzo",&dzo)) dzo = dzt*0.2;
        fzo=fzt;
        nzo=1+(((nzt-1)*dzt)/dzo);

        fzo = fzo*sgn;
        dzo = dzo*sgn;
        ezo = fzo+(nzo-1)*dzo;

        /* error if survey dimensions exceed those of the travel time tables */
	if(fxt>fxso||fxt>fxgo||ext<exso||ext<exgo||fzt>fzo||ezt<ezo) 
		throw cseis_geolib::csException("output range is out of traveltime table!\n");


        if (!parObj.getparint("recsurf",&recsurf)) recsurf=0;
        if (!parObj.getparint("datsurf",&datsurf)) datsurf=0;

        if(recsurf==0){     
           if (!parObj.getparfloat("zrec",&zrec)) throw cseis_geolib::csException("must specify zrec when recsurf=0!\n");
        }     

        if(datsurf==0){     
           if (!parObj.getparfloat("zdat",&zdat)) throw cseis_geolib::csException("must specify zdat when datsurf=0!\n");
        }    


	if (!parObj.getparfloat("v0",&v0)) v0 = 1500;
	if (!parObj.getparfloat("aperx",&aperx)) aperx = 0.5*nxt*dxt;
	if (!parObj.getparfloat("offmax",&offmax)) offmax = 99999;

        if (!parObj.getparfloat("freq",&freq))    freq=50;
        if (freq <= 0.0) throw cseis_geolib::csException("freq must be positive and non-zero!\n");
        if (1/(2*freq)<dt) warn("freq set too high - you may have numerical singularities !\n"); 

        if (!parObj.getparfloat("scale",&scale))    scale=1.0;

	if (!parObj.getparint("ntr",&ntr))	ntr = 100000;
	if (!parObj.getparint("mtr",&mtr))	mtr = 100;

	fprintf(jpfp,"\n");
	fprintf(jpfp," Datuming parameters\n");
	fprintf(jpfp," ================\n");
	fprintf(jpfp," datain=%s \n",datain);
	fprintf(jpfp," dataout=%s \n",dataout);
	fprintf(jpfp," ttfile=%s \n",ttfile);
	fprintf(jpfp," \n");
        fprintf(jpfp," nzt=%d fzt=%g dzt=%g\n",nzt,fzt,dzt);
	fprintf(jpfp," nxt=%d fxt=%g dxt=%g\n",nxt,fxt,dxt);
 	fprintf(jpfp," ns=%d fs=%g ds=%g\n",ns,fs,ds);
	fprintf(jpfp," \n");
        fprintf(jpfp," nxi=%d fxi=%g dxi=%g sgn=%d\n",nxi,fxi,dxi,sgn);
        fprintf(jpfp," \n");
	fprintf(jpfp," nzo=%d fzo=%g dzo=%g\n",nzo,fzo,dzo);
	fprintf(jpfp," nxso=%d fxso=%g dxso=%g\n",nxso,fxso,dxso);
        fprintf(jpfp," nxgo=%d fxgo=%g dxgo=%g\n",nxgo,fxgo,dxgo);
	fprintf(jpfp," \n");
        fprintf(jpfp," nt=%d ft=%g dt=%g \n",nt,ft,dt);
        fprintf(jpfp," freq=%g v0=%g\n",freq,v0);
        fprintf(jpfp," aperx=%g offmax=%g \n",aperx,offmax);
        fprintf(jpfp," ntr=%d mtr=%d \n",ntr,mtr);
        fprintf(jpfp," ================\n");
        fflush(jpfp);


        /* read and create recording and datuming surfaces  */

        if(recsurf==1){
           if(!parObj.getparstring("recfile",&recfile))
                throw cseis_geolib::csException("you chose recsurf=1, so you must specify a recfile!\n");
           recfp = fopen(recfile,"r");
        }

        if(datsurf==1){
           if(!parObj.getparstring("datfile",&datfile))
                throw cseis_geolib::csException("you chose datsurf=1, so you must specify a datfile!\n");
           datfp = fopen(datfile,"r");
        }     

        parObj.checkpars();

        szif = ealloc2float(2,nxi);    


        if(recsurf==0){     
          for(i=0;i<nxi;i++) szif[i][0]=zrec;
        }else{

          for(i=0;i<nxi;i++){
           if(fscanf(recfp,"%f",&r1)<0) throw cseis_geolib::csException("not enough points in recfile!\n");
           szif[i][0]=r1;
          } 
          fclose(recfp);

        }     

        if(datsurf==0){     
          for(i=0;i<nxi;i++) szif[i][1]=zdat;
        }else{

          for(i=0;i<nxi;i++){
           if(fscanf(datfp,"%f",&r2)<0) throw cseis_geolib::csException("not enough points in datfile!\n");
           szif[i][1]=r2;
          } 
          fclose(datfp);

        }      


        /* allocate array memory */
        ttab = ealloc3float(nzt,nxt,ns);
        tsum = alloc2float(nzt,nxt);

        /* array with length equal to the number of shot gathers */
        ng = ealloc1int(nxso);
        memset((void *) ng,(int) '\0',nxso*sizeof(int));

	/* allocate output array */
        dats = ealloc3float(nt,nxgo,nxso);
        memset((void *) dats[0][0],(int) '\0',nxso*nxgo*nt*sizeof(float));


 	fprintf(jpfp," input traveltime tables \n");
                       
        /* loop over shot locations and read the corresponding slice of  */
        /* the travel time array for the shot.   */
	for(is=0; is<ns; ++is){
		nseek = nxt*nzt*is;
		fseek(ttfp,nseek*((long) sizeof(float)),0);
		fread(ttab[is][0],sizeof(float),nxt*nzt,ttfp);
	}


/*  begin main loop.   */


/*  program flow and loop structure:  the outermost (main) loop cycles once   */
/*  through the data traces.  for each trace, loop over the output section,   */
/*  (over an aperture centered on the offset of the data trace), adding to    */
/*  the sum at each point in the output section [ix,it] the value on the data */
/*  trace at the retarded or advanced time (depending on extrapolation        */
/*  direction.  Schematically:                                                */
/*                                                                            */ 
/*      loop over data traces [X]                                             */
/*                                                                            */
/*       loop over output traces [ix] (in aperture centered at X )            */ 
/*                                                                            */
/*         loop over times along output trace [it]                            */
/*                                                                            */
/*            add to sum at output point [ix,it] the value                    */
/*            on data trace at [X,it+-tau]                                    */ 
/*                                                                            */
/*            (where tau is the travel time between the                       */
/*             location [ix,zdatum] on the datum and the trace                */
/*             location)                                                      */
/*                                                                            */


	fprintf(jpfp," start receiver datuming ... \n");
	fprintf(jpfp," \n");
	fflush(jpfp);

	jtr = 1;
        ktr = 0;
	fxin = fxgo;
        io=0;
        s = fxso;

	for(is=0; is<nxso; ++is)
		ng[is]=0;

	/* Store headers in tmpfile while getting a count */
	hdrfp = etmpfile(); 


	do {

	    sx = (float)tr.sx;
	    gx = (float)tr.gx;

            /* determine whether this is the start of a new gather */
	    if(sx!=s) {
		s = sx;
                if(gx>fxgo) fxin = gx;
	    }

            /* determine which output gather this trace belongs to */
            io = (int)((sx-fxso)/dxso);


	    if(MIN(sx,gx)>=fs && MAX(sx,gx)<=es && 
	       MAX(gx-sx,sx-gx)<=offmax ){

                /* count the number of traces in this sources's gather  */
                ng[io]++;

                efwrite(&tr, 1, HDRBYTES, hdrfp);

                /* interpolate travel time table to receiver location */
	    	as = (gx-fs)/ds;
	    	is = (int)as;
		if(is==ns-1) is=ns-2;
		res = as-is;
		if(res<=0.01) res = 0.0;
		if(res>=0.99) res = 1.0;
		sum2(nxt,nzt,1-res,res,ttab[is],ttab[is+1],tsum);


                /* call to dat2d will take the energy on this data trace */
                /* and remap it to the output along the appropriate      */
                /* operators.                                            */

		dat2d(tr.data,nt,ft,dt,sx,gx,dats[io],aperx,nxgo,fxin,
		      dxgo,nzo,fzo,dzo,nxi,fxi,dxi,
                      tsum,sgn,nzt,fzt,dzt,nxt,fxt,dxt,
                      szif,v0,freq);

	        ktr++;
	        if((jtr-1)%mtr ==0 ){
			fprintf(jpfp," Datumed receiver in trace %d\n",jtr);
			fflush(jpfp);
	    	}

	    }
	    jtr++;
	} while (fgettr(infp,&tr) && jtr<=ntr);

/*  end of main loop  */



/*  output data  */

	fprintf(jpfp," Datumed receivers in %d total traces \n",ktr);

	/* Seg-Y header					*/

	rewind(hdrfp);


        /* scale factors representing the constant in the integral */


        scal = dxgo*(1/sqrt(2*PI*v0)); 
        scal = scal*scale;


	for(io=0; io<nxso; io++)  {
		for(ixo=0; ixo<ng[io]; ixo++) {

			efread(&tro, 1, HDRBYTES, hdrfp);
			memcpy((void *) tro.data,
			   (const void *) dats[io][ixo], nt*sizeof(float));

			for(it=0; it<nt; it++){
			   tro.data[it] *=scal;
                        }

			/* write out */
			fputtr(outfp,&tro); 

		}
	}

	fprintf(jpfp," \n");
	fprintf(jpfp," output done\n");
	fflush(jpfp);

	efclose(jpfp);
	efclose(outfp);
	efclose(hdrfp);

	free1int(ng);	    
	free2float(tsum);
	free3float(ttab);
	free3float(dats);
	su2cs->setEOF();
	pthread_exit(NULL);
	return retPtr;
}
catch( cseis_geolib::csException& exc ) {
  su2cs->setError("%s",exc.getMessage());
  pthread_exit(NULL);
  return retPtr;
}
}


/* sum of two tables	*/
  void sum2(int nx,int nz,float a1,float a2,float **t1,float **t2,float **t)
{
	int ix,iz;

	for(ix=0; ix<nx; ++ix) 
		for(iz=0; iz<nz; ++iz)
			t[ix][iz] = a1*t1[ix][iz]+a2*t2[ix][iz];
}
 

  void dat2d(float *trace,int nt,float ft,float dt,float sx,float gx,
	     float **dat,float aperx,int nxgo,float fxin,float dxgo,
  	     float nzo,float fzo,float dzo,
	     int nxi,float fxi,float dxi,
	     float **tsum,int sgn,
             int nzt,float fzt,float dzt,int nxt,float fxt,float dxt,
             float **szif,float v0,float freq)
             
/*****************************************************************************
Datum receiver on one trace 
******************************************************************************
Input:
*trace		one seismic trace 
nt		number of time samples in seismic trace
ft		first time sample of seismic trace
dt		time sampling interval in seismic trace
sx,gx		lateral coordinates of source and geophone (ignored)
aperx		lateral aperture in the datuming process
nxgo,fxin,dxgo  traces in output
nzo,fzo,dzo     interpolation variables to define surfaces	
fxi             x-coordinate of the first surface location
dxi             horizontal spacing on surface
nxi             number of input surface locations
tsum		sum of residual traveltimes from shot and receiver
nxt,fxt,dxt,nzt,fzt,dzt		dimension parameters of traveltime table
szif            array[] of the z component of the reference surfaces

Output:
dat		Redatumed section
*****************************************************************************/
{
        int nxf,nxe,it,ix,iz,jz,jt,jx,mr;
        float x,z,ampd,res0,am,am0,fxf;
	float ax,ax0,odt=1.0/dt,az,sz,sz0,at,tio,res,temp,h;
	float p,q,s,ctau,xst=0.0,zst=0.0,rig,sing,t0,rog,rs,zi,zsx,zs;
        float phi,xp,hp,gxp,zip,atol;
	float *tzt;
        float zp=0.0,dzde=0.0,g,xs;


        /* set a tolerance variable to deal with numerical problems */
        atol = 1e-10; 

	/* allocate memory for datuming */
	tzt = alloc1float(nzt);

        h = (gx>=sx)?(gx-sx)/2.0:(sx-gx)/2.0;
        fxf = fxi + (nxi-2)*dxi;

        /* call to filt will perform the frequency domain filtering */
        /* on the input data trace, returning in variable trace.    */  
	
  	filt(trace,nt,dt,sgn);            


        /* calculate the limits of the output  */
        nxf = (gx-aperx-fxin)/dxgo;
        if(nxf<0) nxf = 0;
        nxe = (gx+aperx-fxin)/dxgo;
        if(nxe>=nxgo) nxe = nxgo-1;


        /* error if survey length exceeds topography definition */
	am = (fxin-fxi)/dxi;
	mr = (int)am;
	am = am - mr;
	if(am<=0.01) am = 0.;
	if(am>=0.99) am = 1.0;
	am0 = 1.0-am;
	if(mr<0) mr = 0;
	if(mr+nxe>nxi-1)
                throw cseis_geolib::csException("Topography definition is out of range!\n");


        /* depth of the input receiver (zi) on the recording surface */
        am = (gx-fxi)/dxi;
        mr = (int)am;
        am = am - mr;
        if(am<=0.01) am = 0.;
        if(am>=0.99) am = 1.0;
        am0 = 1.0-am;
        if(mr<0) mr = 0;

        if(mr>=nxi-1){
           mr=mr-1;
           am0 = 0.0;
           am = 1.0;
        }

        zi = ABS(am0*szif[mr][0]+am*szif[mr+1][0]);
        if(zi>=fzo+(nzo*dzo))
                 throw cseis_geolib::csException("Recording surface is out of range!\n");


        /* approximate the surface derivative near this input location */
        if(mr==0){ 
           zp = szif[mr+1][0]-szif[mr][0];
           dzde = zp/dxi;         
        }else{
           zp = (szif[mr][0]-szif[mr-1][0]) + (szif[mr+1][0]-szif[mr][0]);
           dzde = zp/(2*dxi);
        }


        /* depth of the source location(zsx) */
        am = (sx-fxi)/dxi;
        mr = (int)am;
        am = am - mr;
        if(am<=0.01) am = 0.;
        if(am>=0.99) am = 1.0;
        am0 = 1.0-am;
        if(mr<0) mr = 0;

        if(mr>=nxi-1){
           mr=mr-1;
           am0 = 0.0;
           am = 1.0;
        }

        zsx = ABS(am0*szif[mr][0]+am*szif[mr+1][0]);
        if(zsx>=fzo+(nzo*dzo))
                throw cseis_geolib::csException("Recording surface is out of range!\n");


	
	/* loop over the lateral aperture in the output section	*/
	for(ix=nxf; ix<=nxe; ++ix){
		x = fxin+ix*dxgo;

		ax = (x-fxt)/dxt;
		jx = (int)ax;
		ax = ax-jx;
		if(ax<=0.01) ax = 0.;
		if(ax>=0.99) ax = 1.0;
		ax0 = 1.0-ax;

                /* determine the time tio from the receiver location of this */
                /* input trace to the receiver location of this output trace */
                /* by building a vector of times to all depths, then interp- */
                /* -olating to the exact datuming depth.                     */

  		for(iz=0; iz<nzt; ++iz){                           
		    tzt[iz] = ax0*tsum[jx][iz]+ax*tsum[jx+1][iz];
             	}

		/* datuming depth at this output location (z) */

                am = (x-fxi)/dxi;
                mr = (int)am;
                am = am - mr;
                if(am<=0.01) am = 0.;
                if(am>=0.99) am = 1.0;
                am0 = 1.0-am;
                if(mr<0) mr = 0;

                if(mr>=nxi-1){
                    mr=mr-1;
                    am=0.0; 
                    am0=1.0;
                }

                z = ABS(am0*szif[mr][1]+am*szif[mr+1][1]);
                if(z<=fzo || z>=fzo+(nzo*dzo))                                                    
                        throw cseis_geolib::csException("Datuming surface is out of travel time range!\n");    


                /* determine the travel time between datum and surface */ 
                az = (z-fzo)/dzt;
                jz = (int)az;
                if(jz>=nzt-1) jz = nzt-2;
                sz = az-jz;
                sz0 = 1.0-sz;
                tio = sz0*tzt[jz]+sz*tzt[jz+1];

/*  this is added for straight ray calc  */

/*
                tio = sqrt((x-gx)*(x-gx)+(z-zi)*(z-zi))/v0;       
*/

/*                                       */


                at = (sgn*tio-ft)*odt;

                /* loop over times along the output trace, and add to the */
                /* sum at the output point [ix,it] any amplitude on the   */
                /* data trace at time it+tio.                            */

                jt = (int)at;
                res = ABS(at-jt);
                res0 = 1.0-res;


                /* only receivers on the greater side of this source location are in */
                /* this gather by the required geometry.  if the output location is */
                /* not, do not use it.                                              */


                if(x>=sx){


                   /* define the time along the singular path */
                   sing = sqrt((sx-x)*(sx-x)+(zsx-z)*(zsx-z))/v0;


                   /* define the coordinates in the shifted-rotated frame */
                   if(x==sx){
                     phi = PI/2.0;
                   }else{
                     phi = atan((z-zsx)/(x-sx));      
                   }

                   xp = (x-sx)*cos(phi)+(z-zsx)*sin(phi);
                   hp = 0.5*xp;
                   gxp = (gx-sx)*cos(phi)+(zi-zsx)*sin(phi);
                   zip = -(gx-sx)*sin(phi)+(zi-zsx)*cos(phi);

                   if(hp==0.0) throw cseis_geolib::csException("Datum and recording surface overlap!\n");


                   /* loop over output times */
                   for (it=0; it<nt; it++){
                     if(it+jt >= 0 && jt < nt-it-sgn){
  
                      t0 = ft+(it*dt);
  
                      if(t0<sing+atol){
  
                         ampd=0.0;

                      }else{ 

                          /* calculate the coordinates of the stationary point */
                          /* in the shifted-rotated frame */

                          if(h==0.0){

                             xst=hp+v0*t0/2.0; 
                             zst=0.0;

                          }else{

                             ctau = (v0*t0)*(v0*t0); 
                             q = (ctau-4.0*hp*hp)/(4.0*ctau); 
 
                             p = 2.0*q*(gxp-xp)*xp*zip;      
                             p = p/((zip*zip) + 4.0*q*(gxp-xp)*(gxp-xp));    
 
                             s = q*zip*zip*(ctau - (xp*xp));          
                             s = s/((zip*zip) + 4.0*q*(gxp-xp)*(gxp-xp));                         

                             zst = sqrt(s+(p*p))-p;        
                             xst = ((gxp-xp)/zip)*zst + xp;

                      /*     if(zst<0.0) throw cseis_geolib::csException("Negative zst!\n");  */

                          }


                          /* rotate back to unprimed coords */

                          xs = xst*cos(phi)-zst*sin(phi)+sx;
                          zs = xst*sin(phi)+zst*cos(phi)+zsx;           
                      /*  if(zs<0) throw cseis_geolib::csException("Negative zs...\n");    */

                          /* calculate path lengths */

                          rig = sqrt((xst-gxp)*(xst-gxp) + (zst-zip)*(zst-zip));
                          rog = sqrt((xst-xp)*(xst-xp) + (zst)*(zst));     
                          rs = sqrt((xst)*(xst) + (zst*zst));    

                      /*  if(rig<0. || rog<0. || rs<0.) warn("Negative path length!\n");
                          if(rig<=rog) warn("St. pnt. not below output!\n");  */

                          /* calculate G(xs,zs) */ 

                          g = (zs-zi)-dzde*(xs-gx);

                          /* calculate the amplitude weight */

                          ampd = g*sqrt(rs+rig)/rig;                 
                          ampd = ampd/(sqrt(rs+rog)*sqrt(ABS(rig-rog)));      

                          /* check to see if this is a valid stationary point */
                          if((rig-rog)<(v0/(4*freq))) ampd = 0.0;                           
                          if(zs<0.) ampd = 0.0;
                          if(rig<=rog) ampd=0.0;
                          if(rig<0. || rog<0. || rs<0.) ampd=0.0;


                      }                

	    	      /* find the correct time data, scale by amplitude, and */
                      /* assign to output array. */ 

                      temp=(res0*trace[it+jt]+res*trace[it+jt+sgn])*ampd;
                      dat[ix][it] += temp;     


                    } 
                }

              }
	}

	free1float(tzt);
}

void filt(float *trace,int nt,float dt,int sgn)

/*******************************************************************
  input: 
    trace(nt)	single seismic trace
  output:
    trace(nt) 	filtered and phase-shifted seismic trace 
********************************************************************/
{
	static int nfft, n;
	static float dw, fw;
	int  it,iw,sgnw;
	float temp, const2, amp, omega;
        complex *ct;


        /* later calculations will require multiplication by sqrt(-i) */
        /* which is equivalent to a phase shift of pi/4.  it will be  */
        /* applied in Euler form (cos-isin), so define a constant:    */
        /*                                  const2=cos(pi/4)=sin(pi/4)*/
	const2 = 0.5*sqrt(2.0);

	if(nfft==0) {   
        	/* Set up FFT parameters */

                nfft = npfao(2*nt,4*nt);              

                if (nfft >= SU_NFLTS || nfft >= 720720)
                        throw cseis_geolib::csException("Padded nt=%d -- too big", nfft);

                n = nfft/2;
                dw = 2.0*PI/(nfft*dt);
                fw = -PI/dt;

        }        

        /* allocate complex array for fft */

        ct   = ealloc1complex(nfft);

        /* zero the real and complex parts of ct out to nfft */
        for(it=0; it<nfft; ++it){
             ct[it].r = 0.0;
             ct[it].i = 0.0;
        }
 
        /* put the trace values into the real part of ct- already padded */
        for(it=0; it<nt; ++it){
             ct[it].r = trace[it];
        }


        /* do the transform for t to omega */
        pfacc(1,nfft,ct);



        /* do omega-domain filtering for point source - 2.5D */

           /* do positive frequencies */
           for(iw=0; iw<=n; ++iw){                           
               
                omega = iw*dw; 
                amp = sqrt(ABS(omega));
                sgnw = 1;    

                temp = (ct[iw].r-sgnw*ct[iw].i)*amp*const2;        
                ct[iw].i = (sgnw*ct[iw].r + ct[iw].i)*amp*const2;  
                ct[iw].r = temp;                                      
           }                                 

           /* do negative frequencies */
           for(iw=n+1; iw<=nfft-1; ++iw){

                omega = fw+(iw-n)*dw;
                amp = sqrt(ABS(omega));
                sgnw = -1;     

                temp = (ct[iw].r-sgnw*ct[iw].i)*amp*const2;        
                ct[iw].i = (sgnw*ct[iw].r + ct[iw].i)*amp*const2;  
                ct[iw].r = temp;                                      
           }                               


        /* do the inverse transform for omega to t */

        pfacc(-1, nfft, ct);            
                
        for (it=0; it<nt; ++it) {
             trace[it] = ct[it].r/nfft;
        }

        free1complex(ct);
}

} // END namespace
