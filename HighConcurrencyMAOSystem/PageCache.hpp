#pragma once

#include "Common.hpp"
#include "ObjectPool.hpp"

class PageCache
{
public:
	static PageCache* GetInstance() // 单例模式
	{
		return &_sInst;
	}

	Span* NewSpan(size_t k); // 获取一个K页的span给CentralCache
	std::mutex _pageMtx;

	Span* MapObjectToSpan(void* obj); // 获取从对象到span的映射

	void ReleaseSpanToPageCache(Span* span); // 释放空闲span回到Pagecache，合并相邻的span

private:
	SpanList _spanLists[NPAGES];
	PageCache()
	{}
	PageCache(const PageCache&) = delete;
	static PageCache _sInst;

	std::unordered_map<PAGE_ID, Span*> _idSpanMap;

	ObjectPool<Span> _spanPool;
};

/////////////////////////////////////////////下面是函数实现
PageCache PageCache::_sInst;

Span* PageCache::NewSpan(size_t k) // 获取一个K页的span
{
	assert(k > 0 && k < NPAGES);

	if (k > NPAGES - 1) //大于128页直接找堆申请
	{
		void* ptr = SystemAlloc(k);
		//Span* span = new Span;
		Span* span = _spanPool.New();

		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k;

		_idSpanMap[span->_pageId] = span; // 建立页号与span之间的映射
		return span;
	}

	if (!_spanLists[k].Empty()) // 先检查第k个桶里面有没有span
	{
		//return _spanLists[k].PopFront(); // 有就直接返回
		Span* kSpan = _spanLists[k].PopFront();

		//建立页号与span的映射，方便central cache回收小块内存时查找对应的span
		for (PAGE_ID i = 0; i < kSpan->_n; i++)
		{
			_idSpanMap[kSpan->_pageId + i] = kSpan;
		}
		return kSpan;
	}

	// 检查一下后面的桶里面有没有span，如果有->进行切分
	for (size_t i = k + 1; i < NPAGES; ++i)
	{
		if (!_spanLists[i].Empty())
		{
			Span* nSpan = _spanLists[i].PopFront();
			//Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();

			// 在nSpan的头部切一个k页span下来返回，nSpan再挂到对应映射的位置
			kSpan->_pageId = nSpan->_pageId; // _pageId类似地址
			kSpan->_n = k;

			nSpan->_pageId += k; // 起始页的页号往后走
			nSpan->_n -= k; // 页数减等k

			_spanLists[nSpan->_n].PushFront(nSpan); // 将剩下的挂到对应映射的位置

			//存储nSpan的首尾页号与nSpan之间的映射，方便page cache合并span时进行前后页的查找
			_idSpanMap[nSpan->_pageId] = nSpan;
			_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;

			//建立页号与span的映射，方便central cache回收小块内存时查找对应的span
			for (PAGE_ID i = 0; i < kSpan->_n; i++)
			{
				_idSpanMap[kSpan->_pageId + i] = kSpan;
			}

			return kSpan;
		}
	}

	// 走到这说明后面没有大页的span了->去找堆要一个128页的span
	//Span* bigSpan = new Span;
	Span* bigSpan = _spanPool.New();
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT; // 转换为页号
	bigSpan->_n = NPAGES - 1;

	_spanLists[bigSpan->_n].PushFront(bigSpan);
	return NewSpan(k); // 把新申请的span插入后再递归调用一次自己(避免代码重复)
}

Span* PageCache::MapObjectToSpan(void* obj)
{
	PAGE_ID id = ((PAGE_ID)obj >> PAGE_SHIFT);

	std::unique_lock<std::mutex> lock(_pageMtx); // 构造时加锁，析构时自动解锁

	auto ret = _idSpanMap.find(id);
	if (ret != _idSpanMap.end())
	{
		return ret->second;
	}
	else
	{
		assert(false);
		return nullptr;
	}
}

void PageCache::ReleaseSpanToPageCache(Span* span)
{
	if (span->_n > NPAGES - 1) //大于128页直接释放给堆
	{
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		//delete span;
		_spanPool.Delete(span);
		return;
	}

	while (true) // 对span前后的页，尝试进行合并，缓解内存碎片问题
	{
		PAGE_ID prevId = span->_pageId - 1;
		auto ret = _idSpanMap.find(prevId);
		if (ret == _idSpanMap.end()) // 前面的页号没有，不合并
		{
			break;
		}

		Span* prevSpan = ret->second; // 前面相邻页的span在使用，不合并
		if (prevSpan->_isUse == true)
		{
			break;
		}

		if (prevSpan->_n + span->_n > NPAGES - 1) // 合并出超过128页的span没办法管理，不合并
		{
			break;
		}

		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;

		_spanLists[prevSpan->_n].Erase(prevSpan);
		//delete prevSpan;
		_spanPool.Delete(prevSpan);
	}

	while (true) // 向后合并
	{
		PAGE_ID nextId = span->_pageId + span->_n;
		auto ret = _idSpanMap.find(nextId);
		if (ret == _idSpanMap.end())
		{
			break;
		}

		Span* nextSpan = ret->second;
		if (nextSpan->_isUse == true)
		{
			break;
		}

		if (nextSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		span->_n += nextSpan->_n;

		_spanLists[nextSpan->_n].Erase(nextSpan);
		//delete nextSpan;
		_spanPool.Delete(nextSpan);
	}

	_spanLists[span->_n].PushFront(span);
	span->_isUse = false;
	_idSpanMap[span->_pageId] = span;
	_idSpanMap[span->_pageId + span->_n - 1] = span;
}