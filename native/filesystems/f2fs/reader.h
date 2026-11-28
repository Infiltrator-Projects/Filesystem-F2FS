/* SPDX-License-Identifier: GPL-3.0-or-later
 * Bounded offline F2FS reader. Native shadow-table selection and checkpoint
 * journals are required; simply looking at the first NAT/SIT copy is unsafe.
 * This initial profile admits a clean conventional 4 KiB, single-device,
 * non-encrypted volume with no incompatible inode layout extensions.
 */
#define B 4096u
#define MAIN_MAX 1048576u /* bound the native segment map to 65 MiB (2 TiB main area) */
typedef struct {
 const IfsFsHostRuntimeV2*h;
 uint32_t main,segments,sit,nat,sit_blocks,nat_blocks;
 uint64_t user,valid,version;
 uint8_t cp[B],nat_j[507],sit_j[507];
 uint8_t *maps;
} Volume;
static void copy_bytes(void*d,const void*s,unsigned n){uint8_t*p=d;const uint8_t*q=s;while(n--)*p++=*q++;}
static void clear_bytes(void*d,unsigned n){uint8_t*p=d;while(n--)*p++=0;}
static int msb(const uint8_t*p,uint32_t bit){return (p[bit/8u]>>(7u-bit%8u))&1u;}
static int lsb(const uint8_t*p,uint32_t bit){return (p[bit/8u]>>(bit%8u))&1u;}
static int main_block(const Volume*v,uint32_t address,int node)
{
 if(address<v->main||(uint64_t)address-v->main>=(uint64_t)v->segments*512u)return 0;
 uint32_t rel=address-v->main,seg=rel/512u;
 const uint8_t*p=v->maps+seg*65u;
 return (node?p[64]>=3u:p[64]<3u)&&msb(p,rel%512u);
}
static int disk_block(const Volume*v,uint32_t b,uint8_t*p)
{return read_bytes(v->h,(uint64_t)b*B,p,B);}
static int journals(Volume*v,uint32_t start)
{
 uint8_t p[B];unsigned n=le32(v->cp+136),summary=le32(v->cp+140),flags=le32(v->cp+132);
 int rc;
 if(flags&4u){
  if(summary>=n-4u)return IFS_FS_CORRUPT;
  rc=disk_block(v,start+summary,p);if(rc)return rc;
  copy_bytes(v->nat_j,p,507);copy_bytes(v->sit_j,p+507,507);
 }else{
  if(n<8u||summary!=n-7u)return IFS_FS_CORRUPT;
  rc=disk_block(v,start+n-7u,p);if(rc)return rc;
  copy_bytes(v->nat_j,p+3584,507);
  rc=disk_block(v,start+n-5u,p);if(rc)return rc;
  copy_bytes(v->sit_j,p+3584,507);
 }
 unsigned a=le16(v->nat_j),b=le16(v->sit_j);
 if(a>38u||b>6u)return IFS_FS_CORRUPT;
 for(unsigned i=0;i<a;i++){
  uint32_t id=le32(v->nat_j+2+13u*i);
  if(id<1u||(uint64_t)id>=(uint64_t)v->nat_blocks*455u)return IFS_FS_CORRUPT;
  for(unsigned j=0;j<i;j++)if(le32(v->nat_j+2+13u*j)==id)return IFS_FS_CORRUPT;
 }
 for(unsigned i=0;i<b;i++){
  uint32_t id=le32(v->sit_j+2+78u*i);
  if(id>=v->segments)return IFS_FS_CORRUPT;
  for(unsigned j=0;j<i;j++)if(le32(v->sit_j+2+78u*j)==id)return IFS_FS_CORRUPT;
 }
 return IFS_FS_OK;
}
/* Reconstruct current segment allocation from the selected SIT shadow
 * plus its journal. Check every bit count against the native record,
 * and reconcile total live blocks against the selected checkpoint. */
static int load_sit(Volume*v)
{
 uint8_t p[B];uint32_t cached=UINT32_MAX;uint64_t sum=0;
 for(uint32_t seg=0;seg<v->segments;seg++){
  uint32_t index=seg/55u;
  if(index>=v->sit_blocks)return IFS_FS_CORRUPT;
  if(cached!=index){
   int rc=disk_block(v,v->sit+index+(msb(v->cp+192,index)?v->sit_blocks:0u),p);
   if(rc)return rc;
   cached=index;
  }
  const uint8_t*e=p+74u*(seg%55u);
  for(unsigned j=0;j<le16(v->sit_j);j++){
   const uint8_t*q=v->sit_j+2+78u*j;if(le32(q)==seg){e=q+4;break;}
  }
  unsigned count=le16(e)&1023u,type=le16(e)>>10,bits=0;
  for(unsigned k=0;k<512u;k++)if(msb(e+2,k))bits++;
  if(count!=bits||type>5u)return IFS_FS_CORRUPT;
  sum+=bits;
  copy_bytes(v->maps+seg*65u,e+2,64);v->maps[seg*65u+64]=(uint8_t)type;
 }
 return sum==v->valid?IFS_FS_OK:IFS_FS_CORRUPT;
}
static int node(const Volume*v,uint32_t id,uint32_t owner,uint8_t*out)
{
 uint8_t p[B];uint32_t index=id/455u;
 if(id<3u||index>=v->nat_blocks)return IFS_FS_CORRUPT;
 const uint8_t*e=NULL;
 for(unsigned j=0;j<le16(v->nat_j);j++){
  const uint8_t*q=v->nat_j+2+13u*j;if(le32(q)==id){e=q+4;break;}
 }
 if(!e){
  uint32_t addr=v->nat+2u*index-(index%512u);
  if(msb(v->cp+192+le32(v->cp+156),index))addr+=512u;
  int rc=disk_block(v,addr,p);if(rc)return rc;
  e=p+9u*(id%455u);
 }
 uint32_t ino=le32(e+1),physical=le32(e+5);
 if(ino!=owner||!main_block(v,physical,1))return IFS_FS_CORRUPT;
 int rc=disk_block(v,physical,out);if(rc)return rc;
 return le32(out+4072)==id&&le32(out+4076)==owner?IFS_FS_OK:IFS_FS_CORRUPT;
}
static int inode(const Volume*v,uint32_t id,uint8_t*p)
{
 int rc=node(v,id,id,p);if(rc)return rc;
 unsigned mode=le16(p)&0170000u;
 if((mode!=0100000u&&mode!=0040000u&&mode!=0120000u)||!le32(p+12)||le64(p+16)>v->user*B||
    le32(p+4080)>>3)return IFS_FS_CORRUPT;
 if(p[3]&0xe0u||le32(p+76))return IFS_FS_UNSUPPORTED;
 return IFS_FS_OK;
}
static unsigned xattrs(const uint8_t*p){return (p[3]&5u)?50u:0u;}
static unsigned inline_capacity(const uint8_t*p){return (922u-xattrs(p))*4u;}
/* Ordinary inode/direct-node mappings. Higher indirection is explicitly
 * refused in this bounded reader; absent data pointers are real holes. */
static int mapping(const Volume*v,const uint8_t*i,uint64_t k,uint32_t*address)
{
 unsigned direct=923u-xattrs(i);*address=0;
 if(i[3]&6u)return IFS_FS_UNSUPPORTED;
 if(k<direct)*address=le32(i+360+(unsigned)k*4u);
 else {
  k-=direct;if(k>=2036u)return IFS_FS_UNSUPPORTED;
  uint32_t which=(uint32_t)(k/1018u),nid=le32(i+4052+which*4u);
  if(!nid)return IFS_FS_OK;
  uint8_t p[B];int rc=node(v,nid,le32(i+4072),p);if(rc)return rc;
  if((le32(p+4080)>>3)!=which+1u)return IFS_FS_CORRUPT;
  *address=le32(p+(unsigned)(k%1018u)*4u);
 }
 if(*address==0xffffffffu||*address==0xfffffffeu)return IFS_FS_UNSUPPORTED;
 if(*address&&!main_block(v,*address,0))return IFS_FS_CORRUPT;
 return IFS_FS_OK;
}
static int file_read(const Volume*v,const uint8_t*i,uint64_t off,void*dst,uint32_t n)
{
 uint64_t size=le64(i+16);uint8_t*p=dst;
 if((!dst&&n)||off>size||n>size-off)return IFS_FS_INVALID;
 if(i[3]&2u){
  if(size>inline_capacity(i))return IFS_FS_CORRUPT;
  if(!(i[3]&8u)){if(n)clear_bytes(dst,n);return IFS_FS_OK;}
  if(n)copy_bytes(dst,i+364+(unsigned)off,n);
  return IFS_FS_OK;
 }
 while(n){
  uint32_t address;int rc=mapping(v,i,off/B,&address);if(rc)return rc;
  unsigned skip=(unsigned)(off%B),take=B-skip;if(take>n)take=n;
  if(!address)clear_bytes(p,take);
  else {rc=read_bytes(v->h,(uint64_t)address*B+skip,p,take);if(rc)return rc;}
  p+=take;n-=take;off+=take;
 }
 return IFS_FS_OK;
}
typedef struct {const char*name;unsigned length;uint32_t index,id;char found[256];unsigned found_length;int matched;} Find;
/* The directory bitmap uses least-significant-bit ordering, unlike
 * NAT/SIT version maps. A continuation slot is part of the same name,
 * not an additional directory entry. */
static int directory_data(const uint8_t*p,unsigned bytes,unsigned slots,Find*f)
{
 unsigned head=bytes-slots*19u;
 if(!slots||slots>bytes/19u||head<(slots+7u)/8u)return IFS_FS_CORRUPT;
 const uint8_t*entries=p+head,*names=entries+slots*11u;
 for(unsigned k=0;k<slots;){
  if(!lsb(p,k)){k++;continue;}
  const uint8_t*d=entries+k*11u;unsigned len=le16(d+8),span=(len+7u)/8u;
  uint32_t id=le32(d+4);
  if(!len||len>255u||span>slots-k||id<3u||d[10]>=8u)return IFS_FS_CORRUPT;
  for(unsigned j=0;j<span;j++)if(!lsb(p,k+j))return IFS_FS_CORRUPT;
  const uint8_t*name=names+8u*k;
  for(unsigned j=0;j<len;j++)if(!name[j]||name[j]=='/')return IFS_FS_CORRUPT;
  int dots=(len==1u&&name[0]=='.')||(len==2u&&name[0]=='.'&&name[1]=='.');
  if(!dots){
   int match=f->name?(f->length==len&&equal((const uint8_t*)f->name,name,len)):f->index==0;
   if(match){
    if(f->matched)return IFS_FS_CORRUPT;
    f->matched=1;f->id=id;f->found_length=len;copy_bytes(f->found,name,len);f->found[len]=0;
   }
   if(!f->name&&f->index)f->index--;
   else if(!f->name&&match)f->index=UINT32_MAX;
  }
  k+=span;
 }
 return IFS_FS_OK;
}
static int directory(const Volume*v,const uint8_t*i,Find*f)
{
 if((le16(i)&0170000u)!=0040000u)return IFS_FS_INVALID;
 int rc;
 if(i[3]&4u){
  unsigned capacity=inline_capacity(i),slots=capacity*8u/153u;
  rc=directory_data(i+364,capacity,slots,f);if(rc)return rc;
 }else{
  uint64_t bytes=le64(i+16);
  if(bytes%B||bytes/B>2959u)return IFS_FS_UNSUPPORTED;
  uint8_t p[B];
  for(uint64_t k=0;k<bytes/B;k++){
   uint32_t address;rc=mapping(v,i,k,&address);if(rc)return rc;
   if(!address)continue;
   rc=disk_block(v,address,p);if(rc)return rc;
   rc=directory_data(p,B,214u,f);if(rc)return rc;
  }
 }
 return f->matched?IFS_FS_OK:f->name?IFS_FS_NOT_FOUND:IFS_FS_END;
}
static int query_volume(void*p,IfsFsVolumeInfoV2*info)
{
 Volume*v=p;if(!v||!info||info->structure_bytes<sizeof(*info))return IFS_FS_INVALID;
 info->variant=1;info->block_bytes=B;info->max_name_bytes=255;
 info->total_bytes=v->h->volume_bytes;info->free_bytes=(v->user-v->valid)*B;return IFS_FS_OK;
}
static void destroy(Volume*v)
{if(v->maps)v->h->release(v->h->context,v->maps);v->h->release(v->h->context,v);}
static int mount_volume(const IfsFsHostRuntimeV2*h,void**out,IfsFsVolumeInfoV2*info)
{
 if(!out||!info||info->structure_bytes<sizeof(*info)||!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
 *out=NULL;
 if((h->capabilities&(IFS_FS_HOST_READ_ONLY|IFS_FS_HOST_EXCLUSIVE))!=(IFS_FS_HOST_READ_ONLY|IFS_FS_HOST_EXCLUSIVE))return IFS_FS_ACCESS;
 uint32_t variant;int rc=identify(h,&variant);if(rc)return rc;
 uint8_t s[3072],other[3072],a[B],b[B];
 rc=read_bytes(h,1024,s,sizeof(s));if(rc)return rc;
 rc=read_bytes(h,5120,other,sizeof(other));if(rc)return rc;
 if(sb(s,h->volume_bytes)||sb(other,h->volume_bytes)||!equal(s+8,other+8,116))return IFS_FS_UNSUPPORTED;
 if(le32(s+1664)||le32(s+2180)&~0x804u||s[2201])return IFS_FS_UNSUPPORTED;
 uint32_t cp=le32(s+76),segments=le32(s+68);uint64_t main=(uint64_t)segments*512u;
 if(segments>MAIN_MAX)return IFS_FS_UNSUPPORTED;
 int x=checkpoint(h,cp,main),y=checkpoint(h,cp+512u,main);
 if(x&&y)return IFS_FS_CORRUPT;
 if(!x){rc=read_bytes(h,(uint64_t)cp*B,a,B);if(rc)return rc;}
 if(!y){rc=read_bytes(h,(uint64_t)(cp+512u)*B,b,B);if(rc)return rc;}
 const uint8_t*c=a;
 /* Unsigned subtraction implements the native wrap-aware version order. */
 if(x||(!y&&le64(b)!=le64(a)&&(le64(b)-le64(a))< (UINT64_C(1)<<63))){c=b;cp+=512u;}
 uint32_t flags=le32(c+132),sit=le32(s+56)*256u,nat=le32(s+60)*256u,cs=le32(c+164);
 if(!(flags&1u)||(flags&~0x1c5u)||cs!=4092u||
    le32(c+156)!=(sit+7u)/8u||le32(c+160)!=(nat+7u)/8u||
    (uint64_t)192u+le32(c+156)+le32(c+160)>cs)return IFS_FS_UNSUPPORTED;
 Volume*v=h->allocate(h->context,sizeof(*v));if(!v)return IFS_FS_NO_MEMORY;
 clear_bytes(v,sizeof(*v));v->h=h;v->main=le32(s+92);v->segments=segments;
 v->sit=le32(s+80);v->nat=le32(s+84);v->sit_blocks=sit;v->nat_blocks=nat;
 v->user=le64(c+8);v->valid=le64(c+16);v->version=le64(c);copy_bytes(v->cp,c,B);
 v->maps=h->allocate(h->context,(size_t)segments*65u);
 if(!v->maps){destroy(v);return IFS_FS_NO_MEMORY;}
 rc=journals(v,cp);if(!rc)rc=load_sit(v);
 uint8_t root[B];if(!rc)rc=inode(v,3,root);
 if(!rc){Find f={.index=UINT32_MAX};rc=directory(v,root,&f);if(rc==IFS_FS_END)rc=0;}
 if(rc){destroy(v);return rc;}
 *out=v;return query_volume(v,info);
}
static int unmount_volume(void*p){if(!p)return IFS_FS_INVALID;destroy(p);return IFS_FS_OK;}
static int resolve(const Volume*v,const char*path,uint8_t*i,const char**name,unsigned*len)
{
 if(!v||!path||*path!='/')return IFS_FS_INVALID;
 int rc=inode(v,3,i);if(rc)return rc;*name=path;*len=1;
 const char*p=path+1;
 for(unsigned depth=0;*p;depth++){
  if(depth>=64u)return IFS_FS_UNSUPPORTED;
  unsigned n=0;while(p[n]&&p[n]!='/'){if(n==255u)return IFS_FS_INVALID;n++;}
  if(!n||(n==1u&&p[0]=='.')||(n==2u&&p[0]=='.'&&p[1]=='.'))return IFS_FS_INVALID;
  Find f={.name=p,.length=n};rc=directory(v,i,&f);if(rc)return rc;
  rc=inode(v,f.id,i);if(rc)return rc;
  *name=p;*len=n;p+=n;if(*p=='/'){p++;if(!*p)return IFS_FS_INVALID;}
 }
 return IFS_FS_OK;
}
static int fill(const uint8_t*i,const char*name,unsigned len,IfsFsEntryV2*e)
{
 if(!e||e->structure_bytes<sizeof(*e)||!e->name||len>=e->name_capacity)return IFS_FS_INVALID;
 unsigned mode=le16(i)&0170000u;
 e->type=mode==0040000u?IFS_FS_ENTRY_DIRECTORY:mode==0100000u?IFS_FS_ENTRY_FILE:IFS_FS_ENTRY_LINK;
 e->size_bytes=le64(i+16);e->name_bytes=len;e->attributes=0;
 copy_bytes(e->name,name,len);e->name[len]=0;return IFS_FS_OK;
}
static int stat_path(void*v,const char*path,IfsFsEntryV2*e)
{uint8_t i[B];const char*n;unsigned len;int rc=resolve(v,path,i,&n,&len);return rc?rc:fill(i,n,len,e);}
static int list_path(void*v,const char*path,uint32_t index,IfsFsEntryV2*e)
{
 uint8_t i[B];const char*n;unsigned len;int rc=resolve(v,path,i,&n,&len);if(rc)return rc;
 Find f={.index=index};rc=directory(v,i,&f);if(!rc)rc=inode(v,f.id,i);
 return rc?rc:fill(i,f.found,f.found_length,e);
}
static int read_path(void*v,const char*path,uint64_t off,void*out,uint32_t n)
{
 uint8_t i[B];const char*name;unsigned len;int rc=resolve(v,path,i,&name,&len);if(rc)return rc;
 if((le16(i)&0170000u)!=0100000u)return IFS_FS_INVALID;
 return file_read(v,i,off,out,n);
}
static const IfsFsVolumeOpsV2 volume_ops={.header={2,sizeof(IfsFsVolumeOpsV2),IFS_FS_GROUP_VOLUME,0,IFS_FS_VOLUME_QUERY},.query=query_volume};
