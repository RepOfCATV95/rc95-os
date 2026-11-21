#include "rc95bcd.h"
#include <string.h>
RC95LDR_STATUS Rc95BcdLoadFromVolume(const RC95LDR_VOLUME *Volume,const char *Path,void *Buffer,size_t Capacity,size_t *ImageSize,RC95BCD_HIVE *Hive)
{
 const RC95LDR_FILESYSTEM *Fs; void *Handle=NULL; uint64_t Size=0; RC95LDR_STATUS Status;
 if(!Volume||!Volume->BlockDevice||!Volume->BlockDevice->Read||!Volume->BlockDevice->BlockSize||!Volume->BlockDevice->BlockCount||!Volume->FileSystem||!Path||!Buffer||!ImageSize||!Hive) return Rc95LdrInvalidParameter;
 Fs=Volume->FileSystem; if(!Fs->OpenRead||!Fs->ReadAt||!Fs->Close) return Rc95LdrInvalidParameter;
 *ImageSize=0; memset(Hive,0,sizeof(*Hive));
 Status=Fs->OpenRead(Fs->FileSystemContext,Path,&Handle,&Size); if(Status!=Rc95LdrSuccess) return Status;
 if(Size>(uint64_t)Capacity || Size>(uint64_t)((size_t)-1)) Status=Rc95LdrBufferTooSmall;
 else if(Size<RC95BCD_HIVE_HEADER_SIZE) Status=Rc95LdrInvalidImage;
 else { Status=Fs->ReadAt(Fs->FileSystemContext,Handle,0,Buffer,(size_t)Size);
  if(Status==Rc95LdrSuccess) { Status=Rc95BcdOpenHive(Buffer,(size_t)Size,Hive); if(Status==Rc95LdrSuccess) *ImageSize=(size_t)Size; } }
 Fs->Close(Fs->FileSystemContext,Handle); return Status;
}
