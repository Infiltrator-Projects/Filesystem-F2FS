/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native bounded recognition using the documented on-disk structures and
 * the pinned Linux v6.12 format reference. No kernel, libc or tool backend.
 * Recognition is not evidence of a safe mounted namespace or mutation.
 */
#include <infiltratr/filesystem_module_v2.h>
static inline uint32_t le16(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8);}
static inline uint32_t le32(const uint8_t *p){return le16(p)|(le16(p+2)<<16);}
static inline uint64_t le64(const uint8_t *p){return le32(p)|((uint64_t)le32(p+4)<<32);}
static inline int equal(const uint8_t *a,const uint8_t *b,uint32_t n){
 for(uint32_t i=0;i<n;i++)if(a[i]!=b[i])return 0;
 return 1;
}
static inline uint32_t crc32(uint32_t c,const uint8_t *p,uint32_t n){
 while(n--){c^=*p++;for(unsigned i=0;i<8u;i++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return c;
}
static int read_bytes(const IfsFsHostRuntimeV2 *h,uint64_t off,void *p,uint32_t n){
 if(off>h->volume_bytes||n>h->volume_bytes-off)return IFS_FS_NOT_FOUND;
 return ifs_fs_host_read_bytes_v2(h,off,p,n);
}

#define MAGIC UINT32_C(0xf2f52010)
static int sb(const uint8_t *p, uint64_t media)
{
    if (le32(p)!=MAGIC) return IFS_FS_NOT_FOUND;
    if (le32(p+16)!=12u || le32(p+20)!=9u) return IFS_FS_UNSUPPORTED;
    uint32_t sec=le32(p+8), spb=le32(p+12);
    if (sec<9u || sec>12u || spb!=12u-sec) return IFS_FS_CORRUPT;
    uint64_t blocks=le64(p+36), at=le32(p+72), total=le32(p+48);
    if (!blocks || blocks>media/4096u || !total || at<2u ||
        at+total*512u>blocks || !le32(p+24) || !le32(p+28) ||
        (uint64_t)le32(p+44)*le32(p+24)!=le32(p+68)) return IFS_FS_CORRUPT;
    uint64_t sum=0;
    for (uint32_t i=0;i<5u;i++) {
        uint32_t n=le32(p+52+i*4u);
        if (!n || le32(p+76+i*4u)!=at+sum*512u) return IFS_FS_CORRUPT;
        sum+=n;
    }
    if (sum!=total || le32(p+52)!=2u || (le32(p+56)&1u) || (le32(p+60)&1u) ||
        le32(p+96)!=3u || le32(p+100)!=1u || le32(p+104)!=2u)
        return IFS_FS_CORRUPT;
    /* A nonzero checksum offset declares an on-disk checksum. Verify
     * the conventional packed superblock CRC location, not host memory. */
    uint32_t crc_at=le32(p+32);
    if (crc_at && (crc_at!=3068u || crc32(MAGIC,p,crc_at)!=le32(p+crc_at)))
        return IFS_FS_CORRUPT;
    return IFS_FS_OK;
}
static int checkpoint(const IfsFsHostRuntimeV2 *h,uint64_t start,uint64_t main)
{
    uint8_t p[4096],q[4096]; int rc=read_bytes(h,start*4096u,p,sizeof(p));
    if(rc) return rc;
    uint32_t off=le32(p+164), n=le32(p+136), summary=le32(p+140);
    if(off<192u || off>4092u || n<3u || n>512u || !summary || summary>=n-1u ||
       !le64(p+8) || le64(p+8)>main || le64(p+16)>le64(p+8)) return IFS_FS_CORRUPT;
    uint32_t c=crc32(MAGIC,p,off);
    if(off<4092u) c=crc32(c,p+off+4u,4092u-off);
    if(c!=le32(p+off)) return IFS_FS_CORRUPT;
    rc=read_bytes(h,(start+n-1u)*4096u,q,sizeof(q)); if(rc) return rc;
    off=le32(q+164);
    if(off<192u || off>4092u || le64(q)!=le64(p)) return IFS_FS_CORRUPT;
    c=crc32(MAGIC,q,off); if(off<4092u)c=crc32(c,q+off+4u,4092u-off);
    return c==le32(q+off)?IFS_FS_OK:IFS_FS_CORRUPT;
}
static int identify(const IfsFsHostRuntimeV2 *h,uint32_t *variant)
{
    uint8_t a[3072],b[3072];
    int r=read_bytes(h,1024,a,sizeof(a)); if(r)return r;
    r=read_bytes(h,5120,b,sizeof(b)); if(r)return r;
    int x=sb(a,h->volume_bytes), y=sb(b,h->volume_bytes);
    if(x && y) return x==IFS_FS_NOT_FOUND?y:x;
    const uint8_t *p=x?b:a;
    if(!x&&!y && (!equal(a+8,b+8,100)||!equal(a+108,b+108,16))) return IFS_FS_CORRUPT;
    uint64_t cp=le32(p+76), main=(uint64_t)le32(p+68)*512u;
    x=checkpoint(h,cp,main); y=checkpoint(h,cp+512u,main);
    if(x&&y)return x==IFS_FS_IO_ERROR||y==IFS_FS_IO_ERROR?IFS_FS_IO_ERROR:IFS_FS_CORRUPT;
    *variant=1; return IFS_FS_OK;
}
static int probe(const IfsFsHostRuntimeV2 *h,uint32_t *variant){
 if(!variant||!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
 *variant=0;
 if(h->volume_bytes%h->logical_sector_bytes)return IFS_FS_INVALID;
 return identify(h,variant);
}
#include <infiltratr/filesystem_session_v2.h>
#include "reader.h"
static const IfsFsCoreOpsV2 core={
 .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsCoreOpsV2),IFS_FS_GROUP_CORE,0,IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|IFS_FS_CORE_STAT|IFS_FS_CORE_LIST|IFS_FS_CORE_READ},.probe=probe,.mount=mount_volume,.unmount=unmount_volume,.stat=mounted_stat,.directory_entry=mounted_directory_entry,.read=mounted_read
};
static const IfsFsGroupHeaderV2 *query(uint32_t id,uint32_t min,uint32_t max){
 if(min>2u||max<2u)return NULL;
 if(id==IFS_FS_GROUP_CORE)return &core.header;
 return id==IFS_FS_GROUP_VOLUME?&volume_ops.header:NULL;
}
static const IfsFsModuleApiV2 api={
 .abi_version=2,.structure_bytes=sizeof(IfsFsModuleApiV2),.format_id="f2fs",
#ifdef IFS_FREESTANDING
 .runtime_profiles=IFS_FS_PROFILE_FREESTANDING,
#else
 .runtime_profiles=IFS_FS_PROFILE_HOSTED,
#endif
 .group_mask=IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME),.query_group=query
};
__attribute__((visibility("default"))) const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void){return &api;}
