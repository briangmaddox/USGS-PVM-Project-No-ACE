#ifndef PVMPROJECTOR_CPP_
#define PVMPROJECTOR_CPP_


#include "PvmProjector.h"


//*******************************************************************
PvmProjector::PvmProjector() : Projector(),
 numofslaves(0), childtid(NULL) 
{}

//*******************************************************************
PvmProjector::~PvmProjector()
{
  if (childtid)
    delete [] childtid;
}

//*******************************************************************
void PvmProjector::setNumberOfSlaves(const unsigned int & innumslaves) throw()
{
  try
  {
    numofslaves = innumslaves;
    
    if (childtid)
    {
      delete [] childtid;
      childtid = NULL;
    }

     
    if (numofslaves != 0)
    {
      if (!(childtid = new (std::nothrow) int [numofslaves]))
        throw std::bad_alloc();

    }
  }
  catch(...)
  {
    if (childtid)
    {
      delete [] childtid;
      childtid = NULL;
    }
  }
}

//**********************************************************************
unsigned int PvmProjector::getNumberOfSlaves() const throw()
{
  return numofslaves;
}

  
//*********************************************************************
bool PvmProjector:: project(void (*status)(long int, long int)) 
    throw(ProjectorException)
{
  int mytid;                            //my pvm id
  int errorcode, bufferid, len, tag,    //pvm tags
    temptid;
  long int ycounter;
  bool temp;
  long int numprocess;
  PmeshLib::ProjectionMesh * pmesh = NULL;     //projection mesh
  try
  {
    if (!numofslaves || !childtid)               //must have a slave pvmproject
      return Projector::project(status);

    if (!fromprojection || !toprojection)        //check for projection
      return false;                              //existance
    
    pmesh = setupForwardPmesh();                 //try setup the forward
                                                 //pmesh
    
    if (!getExtents(pmesh))                      //get the extents
      return false;
    
    if(cache)                                    //delete the cache
    {
      delete cache;
      cache = NULL;
    }
      
    if (!setupOutput(outfile))                   //create the output file
      return false;
    
    if (pmesh)                                   //setup reverse mesh
    {
      delete pmesh;
      pmesh = NULL;
    }
    
    mytid = pvm_mytid();                         //enroll in pvm
    errorcode = pvm_spawn("slave", NULL, 0, 
                          NULL, numofslaves, childtid);
    
    if (errorcode = 0)
    {
      return false;                              //can't spawn
    }

    numprocess = 0;
    for (ycounter = 0; ycounter < newheight; ycounter++)
    {
      if (status)
        status(ycounter, newheight);
      bufferid = pvm_recv(-1, -1);             //blocking wait for all messages
      pvm_bufinfo(bufferid, &len, &tag, &temptid);
      

      switch(tag)
      {
      case SETUP_MSG:
        //pack the info in
        sendSlaveSetup();
        //add the first bit of work
        pvm_pklong(&ycounter, 1, 1);
        //send the message
        pvm_send(temptid, SETUP_MSG);
        break;
      case WORK_MSG:
        
        //unpack the scanline
        ++numprocess;
        unpackScanline();
        //pack the next work
        pvm_initsend(PvmDataDefault);
        pvm_pklong(&ycounter, 1, 1);
        pvm_send(temptid, WORK_MSG);
        break;
      case ERROR_MSG:
      default:
        //slave sent error
        return false;
      }
    }

    for (ycounter = 0; ycounter < newheight-numprocess; ycounter++)
    {
      bufferid = pvm_recv(-1, -1);
      pvm_bufinfo(bufferid, &len, &tag, &temptid);
      
      if (tag == WORK_MSG)
      {
        //get the scanline
        unpackScanline();
      }
      else
      {
        return false;                           //should never happen
      }
    }

    //slave termination
    for (ycounter = 0; ycounter < numofslaves; ycounter++)
    {
      pvm_initsend(PvmDataDefault);
      pvm_send(childtid[ycounter], EXIT_MSG);    //tell slave to exit
    }
    writer.removeImage(0);                      //flush the output image
    out = NULL;
    return true;
  }
  catch(...)
  {
    if (pmesh)
    {
      delete pmesh;
      pmesh = NULL;
    }
    return false;
  }
}
    

//*******************************************************************
void PvmProjector::setInputFile(std::string & ininfile) throw()
{
  Projector::setInputFile(ininfile);
  inputfilename = ininfile;
}


//********************************************************************
bool PvmProjector::sendSlaveSetup() throw()
{
  char tempbuffer[100];
  try
  {
    //pack vital info for slave
    pvm_initsend(PvmDataDefault);
    
    strcpy(tempbuffer, inputfilename.c_str());
    //pack the input file name
    pvm_pkstr(tempbuffer);
    //pack the new image metrics
    pvm_pklong(&newheight, 1, 1);
    pvm_pklong(&newwidth, 1, 1);
    pvm_pkdouble(&newscale.x, 1, 1);
    pvm_pkdouble(&newscale.y, 1, 1);
    pvm_pkdouble(&outRect.left, 1, 1);
    pvm_pkdouble(&outRect.top, 1, 1);
    pvm_pkdouble(&outRect.bottom, 1, 1);
    pvm_pkdouble(&outRect.right, 1, 1);

    //pack pmesh info
    pvm_pkint(&pmeshsize, 1, 1);
    pvm_pkint(&pmeshname, 1, 1);
    
    //pack the projection parameters
    pvm_pkint(reinterpret_cast<int *>(&Params.projtype),1,1);
    pvm_pkint(reinterpret_cast<int *>(&Params.datum), 1, 1);
    pvm_pkint(reinterpret_cast<int *>(&Params.unit), 1, 1);
    pvm_pkdouble(&Params.StdParallel1, 1, 1);
    pvm_pkdouble(&Params.StdParallel2, 1, 1);
    pvm_pkdouble(&Params.NatOriginLong, 1,1);
    pvm_pkdouble(&Params.NatOriginLat, 1, 1);
    pvm_pkdouble(&Params.FalseOriginLong, 1, 1);
    pvm_pkdouble(&Params.FalseOriginLat, 1, 1);
    pvm_pkdouble(&Params.FalseOriginEasting, 1, 1);
    pvm_pkdouble(&Params.FalseOriginNorthing, 1, 1);
    pvm_pkdouble(&Params.CenterLong, 1, 1);
    pvm_pkdouble(&Params.CenterLat, 1, 1);
    pvm_pkdouble(&Params.CenterEasting, 1, 1);
    pvm_pkdouble(&Params.CenterNorthing, 1, 1);
    pvm_pkdouble(&Params.ScaleAtNatOrigin, 1, 1);
    pvm_pkdouble(&Params.AzimuthAngle, 1, 1);
    pvm_pkdouble(&Params.StraightVertPoleLong, 1, 1);
    pvm_pkint(&Params.zone, 1, 1);
    pvm_pkdouble(&Params.FalseEasting, 1, 1);
    pvm_pkdouble(&Params.FalseNorthing, 1, 1);
    return true;
  }
  catch(...)
  {
    //don't do anything
    return false;
  }
}


//*******************************************************
void PvmProjector::unpackScanline() throw()
{
  unsigned char * tempscanline=NULL;
  long int scanlinenumber=0;
  
  try
  {
    //create the new scanline
    if (!(tempscanline = new (std::nothrow) unsigned char [newwidth*spp]))
      throw std::bad_alloc();
    
    //unpack the scanline number
    pvm_upklong(&scanlinenumber, 1,1 );

    //unpack the scanline
    pvm_upkbyte(reinterpret_cast<char*>(tempscanline), newwidth*spp, 1);
    out->putRawScanline(scanlinenumber, tempscanline);
    delete [] tempscanline;
}
  catch(...)
  {
    //something bad happened 
    if (tempscanline)
      delete [] tempscanline;
  }
}

#endif








