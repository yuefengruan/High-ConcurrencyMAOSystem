#pragma once

#include "ThreadCache.hpp"
#include "PageCache.hpp"

static void* ConcurrentAlloc(size_t size)
{
	if (size > MAX_BYTES) //大于256KB的内存申请
	{
		//计算出对齐后需要申请的页数
		size_t alignSize = SizeClass::RoundUp(size);
		size_t kPage = alignSize >> PAGE_SHIFT;

		//向page cache申请kPage页的span
		PageCache::GetInstance()->_pageMtx.lock();
		Span* span = PageCache::GetInstance()->NewSpan(kPage);
		span->_objSize = size;
		PageCache::GetInstance()->_pageMtx.unlock();

		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		return ptr;
	}
	else
	{
		// 通过TLS，每个线程无锁地获取自己专属的ThreadCache对象
		if (pTLSThreadCache == nullptr)
		{
			//pTLSThreadCache = new ThreadCache;
			static std::mutex tcMtx;
			static ObjectPool<ThreadCache> tcPool;
			tcMtx.lock();
			pTLSThreadCache = tcPool.New();
			tcMtx.unlock();
		}
		//cout << std::this_thread::get_id() << ":" << pTLSThreadCache << endl;
		return pTLSThreadCache->Allocate(size);
	}
}

static void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
	size_t size = span->_objSize;
	if (size > MAX_BYTES) //大于256KB的内存释放
	{
		Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);

		PageCache::GetInstance()->_pageMtx.lock();
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		PageCache::GetInstance()->_pageMtx.unlock();
	}
	else
	{
		assert(pTLSThreadCache);
		pTLSThreadCache->Deallocate(ptr, size);
	}
}
