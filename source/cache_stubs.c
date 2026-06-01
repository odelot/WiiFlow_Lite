#include <gctypes.h>

extern void PPCDCacheInvalidate(void *, u32);
extern void PPCDCacheFlush(void *, u32);
extern void PPCDCacheStore(void *, u32);
extern void PPCICacheInvalidate(void *, u32);

void DCInvalidateRange(void *addr, u32 len)
{
    PPCDCacheInvalidate(addr, len);
}

void DCFlushRange(void *addr, u32 len)
{
    PPCDCacheFlush(addr, len);
}

void DCStoreRange(void *addr, u32 len)
{
    PPCDCacheStore(addr, len);
}

void ICInvalidateRange(void *addr, u32 len)
{
    PPCICacheInvalidate(addr, len);
}
