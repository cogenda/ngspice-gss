/* Copied and written by Stefan Jones (stefan.jones@multigig.com) at Multigig Ltd
 * Under GPL licence
 * Code based on and copied from ScriptEDA ( http://www-cad.eecs.berkeley.edu/~pinhong/scriptEDA )
 * $Id: tclspice.c,v 1.1.2.5 2003/06/25 12:28:23 stefanjones Exp $	
 */

/*******************/
/*   Defines       */
/*******************/

#define TCLSPICE_name    "spice"
#define TCLSPICE_prefix  "spice::"
#define TCLSPICE_namespace "spice"

/**********************************************************************/
/*              Header files for C functions                          */
/**********************************************************************/

/* Copied from main.c in ngspice*/
#include <ngspice.h>
#include <stdio.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#define _GNU_SOURCE

#include <iferrmsg.h>
#include <ftedefs.h>
#include <devdefs.h>
#include <spicelib/devices/dev.h>
#include <spicelib/analysis/analysis.h>
#include <misc/ivars.h>
#include <frontend/resource.h>
#include <frontend/variable.h>
#include <frontend/outitf.h>
#include <memory.h>


#ifndef HAVE_GETRUSAGE
#ifdef HAVE_FTIME
#include <sys/timeb.h>
#endif
#endif

/*Included for the module to access data*/
#include <dvec.h>
#include <plot.h>
#include <tcl.h>
#include <blt.h>
#include  <sim.h>

/*For get_output*/
#include <fcntl.h>
#include <sys/stat.h>

/* run spicein background */
#include <pthread.h>

#include <stdarg.h>     /* for va_copy() */

extern IFfrontEnd nutmeginfo;

extern struct comm spcp_coms[ ];
 
extern int SIMinit(IFfrontEnd *frontEnd, IFsimulator **simulator);

/*For blt spice to use*/
typedef struct {
  char *name;
  pthread_mutex_t mutex;/*lock for this vector*/
  double *data;/* vector data*/
  int size;/*data it can store*/
  int length;/*actual amount of data*/
} vector;

/*The current run (to get variable names, etc)*/
static runDesc *cur_run = NULL;

static vector *vectors = NULL;

static int ownVectors = 0;

/* save this each time called */
static Tcl_Interp *spice_interp=NULL;
#define save_interp() \
do {\
if ((spice_interp = Tcl_GetMaster(interp)) == NULL)\
      spice_interp = interp;\
} while(0)

/****************************************************************************/
/*                          BLT and data routines                           */
/****************************************************************************/

/*this holds the number of time points done (altered by spice)*/
int steps_completed = 0;
/* number of bltvectors*/
static int blt_vnum = 0;


/*Native Tcl functions */

static int spice_header(ClientData clientData, Tcl_Interp *interp, 
			int args, char **argv){
  char buf[256];
  char *date, *name, *title;
  if (args != 1) {
    Tcl_SetResult(interp, "Wrong # args. spice::spice_header",TCL_STATIC);
    return TCL_ERROR;
  }
  if(cur_run){
    Tcl_ResetResult(interp); 
    date = datestring();
    title = cur_run->name;
    name = cur_run->type;
    sprintf(buf,"{title \"%s\"} {name \"%s\"} {date \"%s\"} {variables %u}",title,name,date,cur_run->numData);
    Tcl_AppendResult(interp, (char *)buf,TCL_STATIC);
    return TCL_OK;
  }else return TCL_ERROR;
}


static int spice_data(ClientData clientData, Tcl_Interp *interp, 
		      int args, char **argv){ 
  char buf[256];
  int i, type;
  char *name;
  if (args != 1) {
    Tcl_SetResult(interp, "Wrong # args. spice::spice_data",TCL_STATIC);
    return TCL_ERROR;
  }
  if(blt_vnum){
    Tcl_ResetResult(interp);
    for(i = 0; i < blt_vnum;i++){
      name = vectors[i].name;
      if (substring("#branch", name))
	type = SV_CURRENT;
      else if (cieq(name, "time"))
	type = SV_TIME;
      else if (cieq(name, "frequency"))
	type = SV_FREQUENCY;
      else
	type = SV_VOLTAGE;
      sprintf(buf,"{%s %s} ",name,
	      ft_typenames(type));
      Tcl_AppendResult(interp, (char *)buf, TCL_STATIC);
    }
    return TCL_OK;
  }else return TCL_ERROR;
}

static int resetTriggers();

/*Creates and registers the blt vectors, used by spice*/
void blt_init(void *run) {
  int i;
  cur_run = NULL;
  /* reset varaibles and free*/
  if(vectors){
    resetTriggers();
    for(i = blt_vnum-1, blt_vnum = 0/*stops vector access*/;i >= 0;i--){
      if(ownVectors)
	FREE(vectors[i].data);
      FREE(vectors[i].name);
      pthread_mutex_destroy(&vectors[i].mutex);
    }
    FREE(vectors);
  }
    
    
  /* initilise */
  cur_run = (runDesc *)run;
  vectors = (vector *)MALLOC(cur_run->numData*sizeof(vector));
  for(i = 0;i < cur_run->numData;i++){
    vectors[i].name = copy((cur_run->data[i]).name);
    pthread_mutex_init(&vectors[i].mutex,NULL);
    vectors[i].data = NULL;
    vectors[i].size = 0;
    vectors[i].length = 0;
  }
  ownVectors = cur_run->writeOut;
  blt_vnum = i;/*allows access to vectors*/
  return;
}

/*Adds data to the stored vector*/
void blt_add(int index,double value){ 
  vector *v;
  v = &vectors[index];
  pthread_mutex_lock(&vectors[index].mutex);
  if(!(v->length < v->size)){
    v->size += 100;
    v->data = (double *)REALLOC(v->data,sizeof(double)*v->size);
  }
  v->data[v->length] = value;
  v->length ++;
  pthread_mutex_unlock(&vectors[index].mutex);
  return;
}
  
/* Locks the vector data to stop conflicts*/
void blt_lockvec(int index){
  pthread_mutex_lock(&vectors[index].mutex);
  return;
}


/*links a dvec to a blt vector, used to stop duplication of data when writing to a plot, 
  but makes BLT vectors more unsafe */
void blt_relink(int index,void *tmp){
  struct dvec *v = (struct dvec *)tmp;
  vectors[index].data = v->v_realdata;
  vectors[index].length = v->v_length;
  vectors[index].size = v->v_length;/*silly spice doesn't use v_rlength*/
  pthread_mutex_unlock(&vectors[index].mutex);
  return;
}




/*        Tcl functions to access spice data   */ 

/* This copys the last Spice state vector to the given blt_vector
 * arg1: blt_vector 
 */
static int lastVector(ClientData clientData, Tcl_Interp *interp, 
		      int args, char **argv){
  Blt_Vector *vec;
  char *blt;
  int i;
  double *V;
  if (args != 2) {
    Tcl_SetResult(interp, "Wrong # args. spice::lastVector vecName",TCL_STATIC);
    return TCL_ERROR;
  }
  blt = argv[1];
  if(Blt_GetVector(interp,blt,&vec)){
    Tcl_SetResult(interp, "Bad blt vector ",TCL_STATIC);
    Tcl_AppendResult(interp, (char *)blt, TCL_STATIC);
    return TCL_ERROR;
  }
  if(!(V = (double *)MALLOC(sizeof(double)*blt_vnum))) {
    Tcl_SetResult(interp, "Out of Memory",TCL_STATIC);
    return TCL_ERROR;
  }

  for(i=0;i < blt_vnum;i++){
    pthread_mutex_lock(&vectors[i].mutex);
    V[i] = vectors[i].data[vectors[i].length-1];
    pthread_mutex_unlock(&vectors[i].mutex);
  }
  Blt_ResetVector(vec,V,blt_vnum,
		  blt_vnum,TCL_VOLATILE);
  txfree(V);
  return TCL_OK; 
}

/*agr1: spice variable name
 *arg2: blt_vector 
 *arg3: start copy index, optional
 *arg4: end copy index. optional
 */
static int spicetoblt(ClientData clientData, Tcl_Interp *interp, 
		      int args, char **argv){
  Blt_Vector *vec;
  int index, i;
  char *blt, *var;
  int start=0,end=-1,len;

  if (args < 3 || args > 5) {
    Tcl_SetResult(interp, "Wrong # args. spice::spicetoblt spice_variable vecName ?start? ?end?",TCL_STATIC);
    return TCL_ERROR;
  }

  var = argv[1];
  blt = argv[2];

  for(i=0;i < blt_vnum && strcmp(var,vectors[i].name);i++);

  if(i == blt_vnum) {
    Tcl_SetResult(interp, "Bad spice variable ",TCL_STATIC);
    Tcl_AppendResult(interp, (char *)var, TCL_STATIC);
    return TCL_ERROR;
  } else index = i;

  if(Blt_GetVector(interp,blt,&vec)){
    Tcl_SetResult(interp, "Bad blt vector ",TCL_STATIC);
    Tcl_AppendResult(interp, (char *)blt, TCL_STATIC);
    return TCL_ERROR;
  }


  if(args >= 4)
    start = atoi(argv[3]);
  if(args == 5)
    end   = atoi(argv[4]);
  if(vectors[index].length) {
    pthread_mutex_lock(&vectors[index].mutex);
      
    len = vectors[index].length;
      
    if(start){
      start = start % len;
      if(start < 0)
	start += len;
    }
      
    end = end % len;
    if(end < 0)
      end += len;
      
    len = abs(end - start + 1);
      
    Blt_ResetVector(vec,(vectors[index].data + start),len,
		    len,TCL_VOLATILE);
      
    pthread_mutex_unlock(&vectors[index].mutex);
  }
  return TCL_OK;
}


/******************************************************************/
/*     Main spice command executions and thread control           */
/*****************************************************************/

static pthread_t tid, bgtid=(pthread_t)0;
static bool fl_running = FALSE;
static bool fl_exited = TRUE;


static void *_thread_run(void *string){
  fl_exited = FALSE;
  bgtid = pthread_self();
  cp_evloop((char *)string);
  FREE(string);
  bgtid = (pthread_t)0;
  fl_exited = TRUE;
  return 0;
}

/*Stops a running thread, hopefully */
static int _thread_stop(){
  int timeout = 0;
  if(fl_running) {
    while(!fl_exited && timeout < 100){
      ft_intrpt = TRUE;
      timeout++;
      sleep(1);
    }
    if(!fl_exited) {
      fprintf(stderr,"couldn't stop tclspice\n");
      return TCL_ERROR;
    }
    pthread_join(tid, NULL);
    fl_running = FALSE;
    ft_intrpt = FALSE;
    return TCL_OK;
  }else {
    fprintf(stderr,"spice not running\n");
  }
  return TCL_OK;
}

static int _run(int args,char **argv){
  char buf[1024] = "", *string;
  int i;
  bool fl_bg = FALSE;
  /* run task in background if preceeded by "bg"*/
  if(!strcmp(argv[0],"bg")) {
    args--;
    argv = &argv[1];
    fl_bg = TRUE;
  }

  /*build a char * to pass to cp_evloop */
  for(i=0;i<args;i++) {
    strcat(buf,argv[i]);
    strcat(buf," ");
  }
   

  /* run in the background */
  if(fl_bg){
    if(fl_running) _thread_stop();
    fl_running =TRUE;
    string = copy(buf);/*as buf gets freed fairly quickly*/
    pthread_create(&tid,NULL,_thread_run,(void *)string);
  } else 
    /* halt (pause) a bg run */
    if(!strcmp(argv[0],"halt")){
      return _thread_stop();
    } else
      /* backwards compatability with old command */
      if(!strcmp(argv[0],"stop"))
	if(args > 1)
	  cp_evloop(buf);
	else{
	  _thread_stop();
	  cp_evloop(buf);
	}	
      else {
	/* cannot do anything if spice is running in the bg*/
	if(fl_running){ 
	  if(fl_exited){
	    _thread_stop();
	    cp_evloop(buf);
	  }else
	    fprintf(stderr,"type \"spice stop\" first\n");
	}else{
	  /*do the command*/
	  cp_evloop(buf);
	}
      }
  return TCL_OK;
}
   
static int _tcl_dispatch(ClientData clientData, Tcl_Interp *interp, 
			 int args, char **argv){
  int i;
  save_interp();
  /* Looks backwards through the first command and strips the :: part */ 
  for(i = strlen(argv[0])-1;i > 0;i--)
    if(argv[0][i] == *":")
      argv[0] += i + 1;  
  return _run(args,argv);
}
   

/* Runs the spice command given in spice <cmd>*/
static int _spice_dispatch(ClientData clientData, Tcl_Interp *interp, 
			   int args, char **argv){
  save_interp();
  if(args == 1) return TCL_OK;
  return _run(args-1,&argv[1]);
}

/*Checks if spice is runnuing in the background */
static int running(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]){
  Tcl_SetObjResult(interp,Tcl_NewIntObj((long) (fl_running && !fl_exited)));
  return TCL_OK;
}


/**************************************/
/*  plot manipulation functions       */
/*  only usefull if plots are saved   */
/**************************************/

/*helper function*/
inline static struct plot * get_plot(int plot){
  struct plot *pl;
  pl = plot_list;
  for(;0 < plot;plot--){
    pl = pl->pl_next;
    if(!pl) 
      return (struct plot *)NULL;
  }
  return pl;
}

/*Outputs the names of all variables in the plot */
static int plot_variables(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]){
  struct plot *pl;
  int plot;
  struct dvec *v;
  
  if (argc != 2) {
    Tcl_SetResult(interp, "Wrong # args. spice::plot_variables plot",TCL_STATIC);
    return TCL_ERROR;
  }
  
  plot = atoi(argv[1]);
  
  if(!(pl = get_plot(plot))){
    Tcl_SetResult(interp,"bad plot given",TCL_STATIC);
    return TCL_ERROR;
  }
    
  for(v = pl->pl_dvecs;v;v = v->v_next){
    Tcl_AppendElement(interp,v->v_name);
  }
  return TCL_OK;
}
 
/*returns the value of a variable */
static int plot_get_value(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]){
  struct plot *pl;
  struct dvec *v;
  char *name;
  int plot,index;
    
  if (argc != 4) {
    Tcl_SetResult(interp, "Wrong # args. spice::plot_get_value name plot index",TCL_STATIC);
    return TCL_ERROR;
  }
  
  name = argv[1];
  plot = atoi(argv[2]);
  index = atoi(argv[3]);
  
  if(!(pl = get_plot(plot))){
    Tcl_SetResult(interp, "bad plot",TCL_STATIC);
    return TCL_ERROR;
  }
  for(v = pl->pl_dvecs;v;v = v->v_next)
    if(!strcmp(v->v_name,name)){
      if (index < v->v_length) {
	Tcl_SetObjResult(interp,Tcl_NewDoubleObj((double) v->v_realdata[index]));
	return TCL_OK;
      } else {
	Tcl_SetResult(interp, "bad index",TCL_STATIC);
	return TCL_ERROR;
      }
    }
  Tcl_SetResult(interp, "variable not found",TCL_STATIC);
  return TCL_ERROR;
}


/*The length of the first vector in a plot*/
static int plot_datapoints(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]){
  struct plot *pl;
  struct dvec *v;
  int plot;

  if (argc != 2) {
    Tcl_SetResult(interp, "Wrong # args. spice::plot_datapoints plot",TCL_STATIC);
    return TCL_ERROR;
  }
    
  plot = atoi(argv[1]);
    
  if(!(pl = get_plot(plot))){
    Tcl_SetResult(interp, "bad plot", TCL_STATIC);
    return TCL_ERROR;
  }

  v = pl->pl_dvecs;

  Tcl_SetObjResult(interp,Tcl_NewIntObj((long) v->v_length));// could be very dangeous
  return TCL_OK;
}

/*These functions give you infomation about a plot*/

static int plot_title(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]){
  struct plot *pl;
  int plot;
  if (argc != 2) {
    Tcl_SetResult(interp, "Wrong # args. spice::plot_nvars plot",TCL_STATIC);
    return TCL_ERROR;
  }
  
  plot = atoi(argv[1]);
  
  if(!(pl = get_plot(plot))){
    Tcl_SetResult(interp, "bad plot", TCL_STATIC);
    return TCL_ERROR;
  }
  Tcl_SetObjResult(interp,Tcl_NewStringObj(pl->pl_title,-1));
  return TCL_OK;
}

static int plot_date(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]){
  struct plot *pl;

  int plot;
  if (argc != 2) {
    Tcl_SetResult(interp, "Wrong # args. spice::plot_nvars plot",TCL_STATIC);
    return TCL_ERROR;
  }
  
  plot = atoi(argv[1]);
  
  if(!(pl = get_plot(plot))){
    Tcl_SetResult(interp, "bad plot", TCL_STATIC);
    return TCL_ERROR;
  }
  Tcl_SetObjResult(interp,Tcl_NewStringObj(pl->pl_date,-1));
  return TCL_OK;
}

static int plot_name(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]){
  struct plot *pl;
  int plot;
  if (argc != 2) {
    Tcl_SetResult(interp, "Wrong # args. spice::plot_nvars plot",TCL_STATIC);
    return TCL_ERROR;
  }
  
  plot = atoi(argv[1]);
  
  if(!(pl = get_plot(plot))){
    Tcl_SetResult(interp, "bad plot", TCL_STATIC);
    return TCL_ERROR;
  }
  Tcl_SetObjResult(interp,Tcl_NewStringObj(pl->pl_name,-1));
  return TCL_OK;
}

/*number of variables in a plot*/

static int plot_nvars(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]){
  struct plot *pl;
  struct dvec *v;
  int plot;
  int i=0;
  
  if (argc != 2) {
    Tcl_SetResult(interp, "Wrong # args. spice::plot_nvars plot",TCL_STATIC);
    return TCL_ERROR;
  }
  
  plot = atoi(argv[1]);
  
  if(!(pl = get_plot(plot))){
    Tcl_SetResult(interp, "bad plot",TCL_STATIC);
    return TCL_ERROR;
  }
  for(v = pl->pl_dvecs;v;v = v->v_next)
    i++;
  Tcl_SetObjResult(interp,Tcl_NewIntObj((long) i));
  return TCL_OK;
}


/*******************************************/
/*           Misc functions                */
/*******************************************/

/*Runs a tcl script and returns the output*/
static int get_output(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]) {
  FILE *pipein;
  int tmp_1,tmp_2=0;
  char buf[1024];
  int outfd,outfd2=0;
  save_interp();
  if ((argc < 2) || (argc > 3)) {
    Tcl_SetResult(interp, "Wrong # args. spice::get_output script ?var_for_stderr?",TCL_STATIC);
    return TCL_ERROR;
  }
  tmp_1=dup(1);
  outfd=open("/tmp/tclspice.tmp_out",O_WRONLY|O_CREAT|O_TRUNC,S_IRWXU);
  if(argc==3){ 
    tmp_2=dup(2);
    outfd2=open("/tmp/tclspice.tmp_err",O_WRONLY|O_CREAT|O_TRUNC,S_IRWXU);
  }
  freopen("/tmp/tclspice.tmp_out","w",stdout);
  if(argc==3)freopen("/tmp/tclspice.tmp_err","w",stderr);
  dup2(outfd,1);
  if(argc==3)dup2(outfd2,2);

  Tcl_Eval(interp,argv[1]);

  fclose(stdout);
  close(outfd);
  if(argc==3){
    fclose(stderr);
    close(outfd2);
  }
  dup2(tmp_1, 1);
  close(tmp_1); 
  if(argc==3){
    dup2(tmp_2, 2);
    close(tmp_2); 
  }
  freopen("/dev/fd/1","w",stdout);
  if(argc==3)freopen("/dev/fd/2","w",stderr);
  pipein=fopen("/tmp/tclspice.tmp_out","r");
  if(pipein==NULL){
    fprintf(stderr,"pipein==NULL\n");
  }
  Tcl_ResetResult(interp);
  while(fgets(buf,1024,pipein)!=NULL){
    Tcl_AppendResult(interp, (char *)buf, TCL_STATIC);
  }
  fclose(pipein);
  if(argc==3){
    pipein=fopen("/tmp/tclspice.tmp_err","r");
    Tcl_SetVar(interp,argv[2],"",0);
    while(fgets(buf,1024,pipein)!=NULL){
      Tcl_SetVar(interp,argv[2],buf,TCL_APPEND_VALUE);
    }
    fclose(pipein);
  }
  return TCL_OK;
}

/* Returns the current value of a parameter 
 * has lots of memory leaks
 */
static int get_param(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]){
  wordlist *wl= NULL;
  char *device, *param;
  struct variable *v;
  char buf[128];
  if (argc != 3) {
    Tcl_SetResult(interp, "Wrong # args. spice::get_param device param",TCL_STATIC);
    return TCL_ERROR;
  }
  if (!ft_curckt) {
    Tcl_SetResult(interp, "No circuit loaded ",TCL_STATIC);
    return TCL_ERROR;
  }
    
  device = argv[1];
  param = argv[2];
  /* copied from old_show(wordlist *) */ 
  v = (*if_getparam)(ft_curckt->ci_ckt,
		     &device, param, 0, 0);
  if (!v)
    v = (*if_getparam)(ft_curckt->ci_ckt,
		       &device, param, 0, 1);
  if (v) {
    wl = cp_varwl(v);
    Tcl_SetResult(interp,wl->wl_word,TCL_VOLATILE);
    wl_free(wl);
    tfree(v);
    return TCL_OK;
      
  } else {
    sprintf(buf,"%s in %s not found",param, device);
    Tcl_AppendResult(interp,buf,TCL_STATIC);
  }
  return TCL_ERROR;

}

/* Direct control over the step size
 * Spice will still adjust it to keep accuracy wuithin reltol and abstol
 */
static int delta(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]){
  if (argc < 1 ||argc > 2) {
    Tcl_SetResult(interp, "Wrong # args. spice::delta ?value?",TCL_STATIC);
    return TCL_ERROR;
  }
  if (!ft_curckt) {
    Tcl_SetResult(interp, "No circuit loaded ",TCL_STATIC);
    return TCL_ERROR;
  }
    
  if(argc == 2) {
    ((CKTcircuit *)ft_curckt->ci_ckt)->CKTdelta = atof(argv[1]); 
  }
  /*Ok, as log as string less than 200 chars*/
  sprintf(interp->result,"%G",((CKTcircuit *)ft_curckt->ci_ckt)->CKTdelta);
  return TCL_OK;
}

#include <trandefs.h>
/* Direct control over the maximum stepsize
 * Spice will still adjust it to keep accuracy wuithin reltol and abstol
 */
static int maxstep(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]){
  TRANan *job;
  if (argc < 1 ||argc > 2) {
    Tcl_SetResult(interp, "Wrong # args. spice::maxstep ?value?",TCL_STATIC);
    return TCL_ERROR;
  }
  if (!ft_curckt) {
    Tcl_SetResult(interp, "No circuit loaded ",TCL_STATIC);
    return TCL_ERROR;
  } 
    
  job = (TRANan*)((CKTcircuit *)ft_curckt->ci_ckt)->CKTcurJob;
  if(argc == 2) {
    job->TRANmaxStep = atof(argv[1]);
  }
  /*Ok, as log as string less than 200 chars*/
  sprintf(interp->result,"%G",job->TRANmaxStep);
  return TCL_OK;
    
}


/****************************************/
/*          The Tk frontend for plot    */
/****************************************/

/* Use Tcl_GetStringResult to get canvas size etc. from Tcl */
#include <ftedev.h>
int  sp_Tk_Init(void) {
  /* This is hard coded in C at the mo, use X11 values */
  dispdev->numlinestyles = 8;
  dispdev->numcolors = 20;
  dispdev->width = 1280;
  dispdev->height = 1024;

  return 0;
}

#include <graph.h>
int  sp_Tk_NewViewport(GRAPH *graph) {
  char *result;
  int width, height, fontwidth, fontheight;
  graph->devdep = (char *) NULL;
    
  if(Tcl_GlobalEval(spice_interp,"spice_gr_NewViewport") != TCL_OK) {
    Tcl_ResetResult(spice_interp);
    return 1;
  }
    
  result = Tcl_GetStringResult(spice_interp);
  if(sscanf(result,"%i %i %i %i",&width,&height,&fontwidth, &fontheight) != 4) {
    Tcl_ResetResult(spice_interp);
    return 1;
  }
  graph->absolute.xpos = 0; /* these always seem sensible, let Tcl adjust coods */
  graph->absolute.ypos = 0;
  graph->absolute.width = width;
  graph->absolute.height = height;
  graph->fontwidth = fontwidth;
  graph->fontheight = fontheight;
  Tcl_ResetResult(spice_interp);
  return 0;
}

int  sp_Tk_Close(void) {
  if(Tcl_Eval(spice_interp,"spice_gr_Close") != TCL_OK) {
    Tcl_ResetResult(spice_interp);
    return 1;
  }    
  Tcl_ResetResult(spice_interp);
  return 0;
}

int  sp_Tk_Clear(void) {
  if(Tcl_Eval(spice_interp,"spice_gr_Clear") != TCL_OK) {
    Tcl_ResetResult(spice_interp);
    return 1;
  }
  Tcl_ResetResult(spice_interp);
  return 0;
}

int  sp_Tk_DrawLine(int x1, int y1, int x2, int y2) {
  char buf[1024];
  sprintf(buf,"spice_gr_DrawLine %i %i %i %i",x1, y1, x2, y2);
  if(Tcl_Eval(spice_interp,buf) != TCL_OK) {
    Tcl_ResetResult(spice_interp);
    return 1;
  }
  Tcl_ResetResult(spice_interp);
  return 0;
}

int  sp_Tk_Arc(int x0, int y0, int radius, double theta1, double theta2) {
  char buf[1024];
  sprintf(buf,"spice_gr_Arc %i %i %i %f %f", x0, y0, radius, theta1, theta2);
  if(Tcl_Eval(spice_interp,buf) != TCL_OK) {
    Tcl_ResetResult(spice_interp);
    return 1;
  }    
  Tcl_ResetResult(spice_interp);
  return 0;
}

int  sp_Tk_Text(char *text, int x, int y) {
  char buf[1024];
  sprintf(buf,"spice_gr_Text \"%s\" %i %i",text,x,y);
  if(Tcl_Eval(spice_interp,buf) != TCL_OK) {
    Tcl_ResetResult(spice_interp);
    return 1;
  }
  Tcl_ResetResult(spice_interp);
  return 0;
}

int  sp_Tk_DefineColor(int colorid, double red, double green, double blue) {
  char buf[1024];
  sprintf(buf,"spice_gr_DefineColor %i %g %g %g",colorid, red, green, blue);
  if(Tcl_Eval(spice_interp,buf) != TCL_OK) {
    Tcl_ResetResult(spice_interp);
    return 1;
  }    
  Tcl_ResetResult(spice_interp);
  return 0;
}

int  sp_Tk_DefineLinestyle(int linestyleid, int mask) {
  char buf[1024];
  sprintf(buf,"spice_gr_DefineLinestyle %i %i", linestyleid, mask);
  if(Tcl_Eval(spice_interp,buf) != TCL_OK) {
    Tcl_ResetResult(spice_interp);
    return 1;
  }    
  Tcl_ResetResult(spice_interp);
  return 0;
}

int  sp_Tk_SetLinestyle(int linestyleid) {
  char buf[1024];
  sprintf(buf,"spice_gr_SetLinestyle %i", linestyleid);
  if(Tcl_Eval(spice_interp, buf) != TCL_OK) {
    Tcl_ResetResult(spice_interp);
    return 1;
  }    
  Tcl_ResetResult(spice_interp);
  return 0;
}
  
int  sp_Tk_SetColor(int colorid) {
  char buf[1024];
  sprintf(buf,"spice_gr_SetColor %i", colorid);
  if(Tcl_Eval(spice_interp,buf) != TCL_OK) {
    Tcl_ResetResult(spice_interp);
    return 1;
  }    
  Tcl_ResetResult(spice_interp);
  return 0;
}

int sp_Tk_Update(void) {
  if(Tcl_Eval(spice_interp,"spice_gr_Update") != TCL_OK) {
    Tcl_ResetResult(spice_interp);
    return 1;
  }    
  Tcl_ResetResult(spice_interp);
  return 0;
}


/********************************************************/
/*           The Blt method for plotting                */
/********************************************************/

int blt_plot(struct dvec *y,struct dvec *x){
  Blt_Vector *X_Data=NULL, *Y_Data=NULL;
  char buf[1024];
    
  /* A bug in these functions? , crashes if used so make vectors in Tcl
     Blt_CreateVector(spice_interp,"spice::X_Data",1,&X_Data);
     Blt_CreateVector(spice_interp,"spice::Y_Data",1,&Y_Data);
  */
  Blt_GetVector(spice_interp,"spice::X_Data",&X_Data);
  Blt_GetVector(spice_interp,"spice::Y_Data",&Y_Data);

  if(!X_Data || !Y_Data) {
    fprintf(stderr,"Error: Blt vector X_Data or Y_Data not created\n");
    return 1;
  }

  Blt_ResetVector (Y_Data, y->v_realdata ,y->v_length, y->v_length, TCL_VOLATILE);

  if (x) {
    Blt_ResetVector (X_Data, x->v_realdata, x->v_length, x->v_length, TCL_VOLATILE);
  } else {
    x = y;
    /*TODO: handle complex data properly */
    Blt_ResetVector (X_Data, y->v_realdata, y->v_length,  y->v_length, TCL_VOLATILE);
  }

  sprintf(buf,"spice_gr_Plot %s %s %s %s %s %s",
	  x->v_name, ft_typenames(x->v_type), ft_typabbrev(x->v_type),
	  y->v_name, ft_typenames(y->v_type), ft_typabbrev(y->v_type));

  if(Tcl_Eval(spice_interp,buf) != TCL_OK) {
    Tcl_ResetResult(spice_interp);
    return 1;
  }
    
  Tcl_ResetResult(spice_interp);

  return 0;
}

/********************************************************/
/*             Triggering stuff                         */
/********************************************************/

struct triggerEvent {
  struct triggerEvent *next;
  int vector;
  int type;
  int stepNumber;
  double time;
};


struct triggerEvent *eventQueue=NULL;
struct triggerEvent *eventQueueEnd=NULL;

pthread_mutex_t triggerMutex;

struct watch {
  struct watch *next;
  int vector;/* index of vector to watch */
  int type; /* +ive or -ive trigger */
  int state; /* pretriggered or not */
  double Vmin; /* the boundaries */
  double Vmax;
};

struct watch *watches=NULL;

int Tcl_ExecutePerLoop() {

  struct watch *current;
       
  pthread_mutex_lock(&vectors[0].mutex);
  pthread_mutex_lock(&triggerMutex);

  for(current=watches;current;current = current->next) {
    vector *v;
    v = &vectors[current->vector];
    pthread_mutex_lock(&v->mutex);

    if((current->type > 0 && current->state && v->data[v->length-1] > current->Vmax) ||
       (current->type < 0 && current->state && v->data[v->length-1] < current->Vmin) ) { 
      struct triggerEvent *tmp = (struct triggerEvent *)MALLOC(sizeof(struct triggerEvent));
      
      tmp->next = NULL;
      
      if(eventQueue) {
	eventQueueEnd->next = tmp;
	eventQueueEnd = tmp;
      } else {
	eventQueue = tmp;
      }
	
      eventQueueEnd = tmp;

      tmp->vector = current->vector;
      tmp->type = current->type;
      tmp->stepNumber = vectors[0].length;
      tmp->time = vectors[0].data[vectors[0].length-1];

      current->state = 0;

    } else 
      if((current->type > 0 && v->data[v->length-1] < current->Vmin) || 
	 (current->type < 0 && v->data[v->length-1] > current->Vmax))
	current->state = 1;
    pthread_mutex_unlock(&v->mutex);    
  }
  
  pthread_mutex_unlock(&triggerMutex);
  
  pthread_mutex_unlock(&vectors[0].mutex);
  
  return 0;
}

static int resetTriggers() {
  pthread_mutex_lock(&triggerMutex);
  
  while(watches) {
    struct watch *tmp = watches;
    watches = tmp->next;
    FREE(tmp);
  }

  while(eventQueue) {
    struct triggerEvent *tmp = eventQueue;
    eventQueue = tmp->next;
    FREE(tmp);
  }
  
  eventQueueEnd = NULL;
  
  pthread_mutex_unlock(&triggerMutex);
  return 0;
}


/* Registers a watch for a trigger
 *arg0: Vector Name to watch
 *arg1: Vmin
 *arg2: Vmax
 *arg3: 1 / -1 for +ive(voltage goes +ive) or -ive trigger
 */
static int registerTrigger(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]){
  int i, index;
  char *var;
  struct watch *tmp;
  
  if (argc != 4 && argc != 5) {
    Tcl_SetResult(interp, "Wrong # args. spice::registerTrigger vecName Vmin Vmax ?type?",TCL_STATIC);
    return TCL_ERROR;
  }

  var = argv[1];

  for(i=0;i < blt_vnum && strcmp(var,vectors[i].name);i++);

  if(i == blt_vnum) {
    Tcl_SetResult(interp, "Bad spice variable ",TCL_STATIC);
    Tcl_AppendResult(interp, (char *)var, TCL_STATIC);
    return TCL_ERROR;
  } else index = i;

  pthread_mutex_lock(&triggerMutex);
  
  tmp = (struct watch *)MALLOC(sizeof(struct watch));

  tmp->next = watches;
  watches = tmp;

  watches->vector = index;
  if(argc == 5)
     watches->type = atoi(argv[4]);
  else
    watches->type = 1;
  
  watches->state = 0;
  watches->Vmin = atof(argv[2]);
  watches->Vmax = atof(argv[3]);
  
  pthread_mutex_unlock(&triggerMutex);
  
  return TCL_OK;
}
/*unregisters a trigger
 *arg0: Vector name
 *arg1: type
 */
static int unregisterTrigger(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]){
  int i, index, type;
  char *var;
  struct watch *tmp;
  struct watch **cut;

  if (argc != 2 && argc != 3) {
    Tcl_SetResult(interp, "Wrong # args. spice::unregisterTrigger vecName ?type?",TCL_STATIC);
    return TCL_ERROR;
  }

  var = argv[1];
  
  for(i=0;i < blt_vnum && strcmp(var,vectors[i].name);i++);
  
  if(i == blt_vnum) {
    Tcl_SetResult(interp, "Bad spice variable ",TCL_STATIC);
    Tcl_AppendResult(interp, (char *)var, TCL_STATIC);
    return TCL_ERROR;
  } else index = i;
  
  if(argc == 3)
    type = atoi(argv[4]);
  else
    type = 1;

  pthread_mutex_lock(&triggerMutex);
  
  cut = &watches;
  
  tmp = watches;

  while(tmp)
    if(tmp->vector == index && tmp->type == type) {
      struct watch *t = tmp;
      *cut = tmp->next;
      tmp = tmp->next;
      FREE(t);
    } else {
      cut = &tmp->next;
      tmp = tmp->next;
    }
  
  pthread_mutex_unlock(&triggerMutex);
 
  return TCL_OK;
}

/* returns:
"vecName" "time" "stepNumber" "type"
*/
static int popTriggerEvent(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]){
  
  if (argc != 1) {
    Tcl_SetResult(interp, "Wrong # args. spice::popTriggerEvent",TCL_STATIC);
    return TCL_ERROR;
  } 

  if(eventQueue) {
    struct triggerEvent *popedEvent;
    Tcl_Obj *list;

    pthread_mutex_lock(&triggerMutex);
    
    popedEvent = eventQueue;
    
    eventQueue = popedEvent->next;
      
    
    list = Tcl_NewListObj(0,NULL);
    
    Tcl_ListObjAppendElement(interp,list,Tcl_NewStringObj(vectors[popedEvent->vector].name,strlen(vectors[popedEvent->vector].name)));

    Tcl_ListObjAppendElement(interp,list,Tcl_NewDoubleObj(popedEvent->time));

    Tcl_ListObjAppendElement(interp,list,Tcl_NewIntObj(popedEvent->stepNumber));

    Tcl_ListObjAppendElement(interp,list,Tcl_NewIntObj(popedEvent->type));

    Tcl_SetObjResult(interp,list);

    FREE(popedEvent);
    
    pthread_mutex_unlock(&triggerMutex);
  }
  
  return TCL_OK;
}

static int listTriggers(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]){
  struct watch *tmp;
  Tcl_Obj *list;
  
  if (argc != 1) {
    Tcl_SetResult(interp, "Wrong # args. spice::listTriggers",TCL_STATIC);
    return TCL_ERROR;
  } 
  
  list = Tcl_NewListObj(0,NULL);

  pthread_mutex_lock(&triggerMutex);
  
  for(tmp=watches;tmp;tmp=tmp->next)
    Tcl_ListObjAppendElement(interp,list,Tcl_NewStringObj(vectors[tmp->vector].name,strlen(vectors[tmp->vector].name)));

  pthread_mutex_unlock(&triggerMutex);

  Tcl_SetObjResult(interp,list);

  return TCL_OK;
}


/*******************************************************/
/*  Initialise spice and setup native methods          */
/*******************************************************/

#ifdef __MINGW32__
__declspec(dllexport)
#endif
int Spice_Init(Tcl_Interp *interp) {

  if (interp == 0) return TCL_ERROR;

#ifdef USE_TCL_STUBS
  if(Tcl_InitStubs(interp, (char *)"8.1",0) == NULL) 
	return TCL_ERROR;
#endif
  
  Tcl_PkgProvide(interp, (char*)TCLSPICE_name, (char*)TCLSPICE_version);
    
  Tcl_Eval(interp, "namespace eval " TCLSPICE_namespace " { }");

  save_interp();

  {
    extern void DevInit();
    int i;
    char *key;
    Tcl_CmdInfo infoPtr;
    char buf[256];
    
    ft_rawfile = NULL;
    ivars( );
    
    cp_in = stdin;
    cp_out = stdout;
    cp_err = stderr;
    
    /*timer*/
    init_time( );
    
    /*IFsimulator struct initilised*/
    SIMinit(&nutmeginfo, &ft_sim);
    
    /* program name*/
    cp_program = ft_sim->simulator;
    
    srandom(getpid());
    
    /*parameter fetcher, used in show*/
    if_getparam = spif_getparam;
    
    /*Command prompt stuff */
    ft_cpinit();
    
    /* initilise Tk display */
    DevInit();

    /*parrallel arch support, or not */
    ARCHme = 0;
    ARCHsize = 1;
    
    /* init the mutex */
    pthread_mutex_init(&triggerMutex,NULL);

    /*register functions*/
    for (i = 0;(key = cp_coms[i].co_comname); i++) {
      sprintf(buf,"%s%s",TCLSPICE_prefix,key);
      if(Tcl_GetCommandInfo(interp,buf, &infoPtr)!= 0){
	printf("Command '%s' can not be registered!\n", buf);
      }else{ 
	Tcl_CreateCommand(interp,buf, _tcl_dispatch, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
      }
    }
    
    Tcl_CreateCommand(interp, TCLSPICE_prefix "spice_header", spice_header, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "spice_data", spice_data, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "spicetoblt", spicetoblt, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "lastVector", lastVector, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "spice", _spice_dispatch, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "get_output", get_output, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "bg", _tcl_dispatch, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "halt", _tcl_dispatch, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "get_param", get_param, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "delta", delta, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "maxstep", maxstep, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "running", running, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "plot_variables", plot_variables, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "plot_get_value", plot_get_value, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "plot_datapoints", plot_datapoints, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "plot_title", plot_title, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "plot_date", plot_date, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "plot_name", plot_name, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "plot_nvars", plot_nvars, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "registerTrigger", registerTrigger, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "popTriggerEvent", popTriggerEvent, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "unregisterTrigger", unregisterTrigger, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, TCLSPICE_prefix "listTriggers", listTriggers, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

    Tcl_LinkVar(interp, TCLSPICE_prefix "steps_completed", (char *)&steps_completed, TCL_LINK_READ_ONLY|TCL_LINK_INT);
    Tcl_LinkVar(interp, TCLSPICE_prefix "blt_vnum", (char *)&blt_vnum, TCL_LINK_READ_ONLY|TCL_LINK_INT);
  }
  return TCL_OK;
}

/***************************************/
/* printf wrappers to redirect to puts */
/***************************************/

/* Contributed by Tim Edwards (tim@stravinsky.jhuapl.edu), 2003 */

/*----------------------------------------------------------------------*/
/* Deal with systems which don't define va_copy().                      */
/*----------------------------------------------------------------------*/

#ifndef HAVE_VA_COPY
#ifdef HAVE___VA_COPY
#define va_copy(a, b) __va_copy(a, b)  
#else
#define va_copy(a, b) a = b
#endif 
#endif

/*------------------------------------------------------*/
/* Redefine the vfprintf() functions for use with tkcon */
/*------------------------------------------------------*/

int tcl_vfprintf(FILE *f, const char *fmt, va_list args_in)
{
  va_list args;
  static char outstr[128] = "puts -nonewline std";
  char *outptr, *bigstr = NULL, *finalstr = NULL;
  int i, nchars, result, escapes = 0;

  if((f != stdout && f != stderr) || 
     ( fl_running && bgtid == pthread_self()) )
      return vfprintf(f,fmt,args_in);

  strcpy (outstr + 19, (f == stderr) ? "err \"" : "out \"");
  outptr = outstr;

  va_copy(args, args_in);
  nchars = vsnprintf(outptr + 24, 102, fmt, args);
  va_end(args);

  if (nchars >= 102)
    {
      va_copy(args, args_in);
      bigstr = Tcl_Alloc(nchars + 26);
      strncpy(bigstr, outptr, 24);
      outptr = bigstr;
      vsnprintf(outptr + 24, nchars + 2, fmt, args);
      va_end(args);
    }
  else if (nchars == -1) nchars = 126;

  for (i = 24; *(outptr + i) != '\0'; i++) {
    if (*(outptr + i) == '\"' || *(outptr + i) == '[' ||
	*(outptr + i) == ']' || *(outptr + i) == '\\')
      escapes++;
  }

  if (escapes > 0)
    {
      finalstr = Tcl_Alloc(nchars + escapes + 26);
      strncpy(finalstr, outptr, 24);
      escapes = 0;
      for (i = 24; *(outptr + i) != '\0'; i++)
        {
	  if (*(outptr + i) == '\"' || *(outptr + i) == '[' ||
	      *(outptr + i) == ']' || *(outptr + i) == '\\')
            {
	      *(finalstr + i + escapes) = '\\';
	      escapes++;
            }
	  *(finalstr + i + escapes) = *(outptr + i);
        }
      outptr = finalstr;
    }
    
  *(outptr + 24 + nchars + escapes) = '\"';
  *(outptr + 25 + nchars + escapes) = '\0';

  result = Tcl_Eval(spice_interp, outptr);

  if (bigstr != NULL) Tcl_Free(bigstr);
  if (finalstr != NULL) Tcl_Free(finalstr);
  return nchars;
}

/*----------------------------------------------------------------------*/
/* Reimplement fprintf() as a call to Tcl_Eval().                       */
/*----------------------------------------------------------------------*/

int tcl_fprintf(FILE *f, const char *format, ...)
{
  va_list ap;
  int rtn;
 
  va_start(ap, format);
  rtn = tcl_vfprintf(f, format, ap);
  va_end(ap);

  return rtn;
}

/*----------------------------------------------------------------------*/
/* Reimplement fprintf() as a call to Tcl_Eval().                       */
/*----------------------------------------------------------------------*/

int tcl_printf(const char *format, ...)
{
  va_list ap;
  int rtn;
  
  va_start(ap, format);
  rtn = tcl_vfprintf(stdout, format, ap);
  va_end(ap);
  
  return rtn;
}
/*------------------------------------------------------*/
/* Console output flushing which goes along with the    */
/* routine tcl_vprintf() above.                         */
/*------------------------------------------------------*/

void tcl_stdflush(FILE *f)
{     
  Tcl_SavedResult state;
  static char stdstr[] = "flush stdxxx";
  char *stdptr = stdstr + 9;
  
  if ( fl_running && bgtid == pthread_self())
      return;
  
  Tcl_SaveResult(spice_interp, &state);
  strcpy(stdptr, (f == stderr) ? "err" : "out");
  Tcl_Eval(spice_interp, stdstr);
  Tcl_RestoreResult(spice_interp, &state);
}