#include "../nuphase.h" 
#include "zlib.h"

void makeTrees (int run, const char * base_path = "/home/radio/data_archive", const char * outpath = "/home/radio/stupidly_rootified")
{
	
  TString str; 
  str.Form("%s/run%d/header/",base_path,run); 
  TSystemDirectory hd_dir(str.Data(), str.Data()); 

  str.Form("%s/header%d.root",outpath,run); 

  TList * hd_files = hd_dir.GetListOfFiles(); 
  hd_files->Sort(); 
  TSystemFile * f; 


  TFile fhd(str.Data(),"RECREATE"); 
  nuphase_header_t *hd = new nuphase_header_t; 
  TTree * thd = new TTree("header","header"); 
  thd->Branch("hd",&hd); 


  TIter next_hd(hd_files); 
 
  while((f = (TSystemFile*) next_hd()))
  {
     if (!strstr(f->GetName(),".gz")) continue; 
     str.Form("%s/run%d/header/%s", base_path, run, f->GetName()); 
     printf("Processing %s\n", str.Data());
     gzFile gzf = gzopen(str.Data(),"r"); 
     fhd.cd(); 	
     while (!nuphase_header_gzread(gzf, hd)) thd->Fill(); 
     gzclose(gzf); 
  }
  
  fhd.cd(); 	
  thd->Write(); 

  

  str.Form("%s/run%d/status/",base_path,run); 

  TSystemDirectory st_dir(str.Data(), str.Data()); 
  TList * st_files = st_dir.GetListOfFiles(); 
  st_files->Sort(); 


  str.Form("%s/status%d.root",outpath,run); 


  TFile fst(str.Data(),"RECREATE"); 
  nuphase_status_t *st = new nuphase_status_t; 

  TTree * tst = new TTree("status","status"); 
  tst->Branch("status",&st); 

  TIter next_st(st_files); 
 
  while((f = (TSystemFile*) next_st()))
  {
     if (!strstr(f->GetName(),".gz")) continue; 
     str.Form("%s/run%d/status/%s", base_path, run, f->GetName()); 
     printf("Processing %s\n", str.Data());
     gzFile gzf = gzopen(str.Data(),"r"); 
     fst.cd();
     while (!nuphase_status_gzread(gzf, st)) tst->Fill(); 
     gzclose(gzf); 
  }
 
  fst.cd();
  tst->Write(); 


}
