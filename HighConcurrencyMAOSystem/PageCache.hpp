#pragma once

#include "Common.hpp"
#include "ObjectPool.hpp"

class PageCache
{
public:
	static PageCache* GetInstance() // ����ģʽ
	{
		return &_sInst;
	}

	Span* NewSpan(size_t k); // ��ȡһ��Kҳ��span��CentralCache
	std::mutex _pageMtx;

	Span* MapObjectToSpan(void* obj); // ��ȡ�Ӷ���span��ӳ��

	void ReleaseSpanToPageCache(Span* span); // �ͷſ���span�ص�Pagecache���ϲ����ڵ�span

private:
	SpanList _spanLists[NPAGES];
	PageCache()
	{}
	PageCache(const PageCache&) = delete;
	static PageCache _sInst;

	std::unordered_map<PAGE_ID, Span*> _idSpanMap;

	ObjectPool<Span> _spanPool;
};

/////////////////////////////////////////////�����Ǻ���ʵ��
PageCache PageCache::_sInst;

Span* PageCache::NewSpan(size_t k) // ��ȡһ��Kҳ��span
{
	assert(k > 0 && k < NPAGES);

	if (k > NPAGES - 1) //����128ҳֱ���Ҷ�����
	{
		void* ptr = SystemAlloc(k);
		//Span* span = new Span;
		Span* span = _spanPool.New();

		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k;

		_idSpanMap[span->_pageId] = span; // ����ҳ����span֮���ӳ��
		return span;
	}

	if (!_spanLists[k].Empty()) // �ȼ���k��Ͱ������û��span
	{
		//return _spanLists[k].PopFront(); // �о�ֱ�ӷ���
		Span* kSpan = _spanLists[k].PopFront();

		//����ҳ����span��ӳ�䣬����central cache����С���ڴ�ʱ���Ҷ�Ӧ��span
		for (PAGE_ID i = 0; i < kSpan->_n; i++)
		{
			_idSpanMap[kSpan->_pageId + i] = kSpan;
		}
		return kSpan;
	}

	// ���һ�º����Ͱ������û��span�������->�����з�
	for (size_t i = k + 1; i < NPAGES; ++i)
	{
		if (!_spanLists[i].Empty())
		{
			Span* nSpan = _spanLists[i].PopFront();
			//Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();

			// ��nSpan��ͷ����һ��kҳspan�������أ�nSpan�ٹҵ���Ӧӳ���λ��
			kSpan->_pageId = nSpan->_pageId; // _pageId���Ƶ�ַ
			kSpan->_n = k;

			nSpan->_pageId += k; // ��ʼҳ��ҳ��������
			nSpan->_n -= k; // ҳ������k

			_spanLists[nSpan->_n].PushFront(nSpan); // ��ʣ�µĹҵ���Ӧӳ���λ��

			//�洢nSpan����βҳ����nSpan֮���ӳ�䣬����page cache�ϲ�spanʱ����ǰ��ҳ�Ĳ���
			_idSpanMap[nSpan->_pageId] = nSpan;
			_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;

			//����ҳ����span��ӳ�䣬����central cache����С���ڴ�ʱ���Ҷ�Ӧ��span
			for (PAGE_ID i = 0; i < kSpan->_n; i++)
			{
				_idSpanMap[kSpan->_pageId + i] = kSpan;
			}

			return kSpan;
		}
	}

	// �ߵ���˵������û�д�ҳ��span��->ȥ�Ҷ�Ҫһ��128ҳ��span
	//Span* bigSpan = new Span;
	Span* bigSpan = _spanPool.New();
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT; // ת��Ϊҳ��
	bigSpan->_n = NPAGES - 1;

	_spanLists[bigSpan->_n].PushFront(bigSpan);
	return NewSpan(k); // ���������span������ٵݹ����һ���Լ�(��������ظ�)
}

Span* PageCache::MapObjectToSpan(void* obj)
{
	PAGE_ID id = ((PAGE_ID)obj >> PAGE_SHIFT);

	std::unique_lock<std::mutex> lock(_pageMtx); // ����ʱ����������ʱ�Զ�����

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
	if (span->_n > NPAGES - 1) //����128ҳֱ���ͷŸ���
	{
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		//delete span;
		_spanPool.Delete(span);
		return;
	}

	while (true) // ��spanǰ���ҳ�����Խ��кϲ��������ڴ���Ƭ����
	{
		PAGE_ID prevId = span->_pageId - 1;
		auto ret = _idSpanMap.find(prevId);
		if (ret == _idSpanMap.end()) // ǰ���ҳ��û�У����ϲ�
		{
			break;
		}

		Span* prevSpan = ret->second; // ǰ������ҳ��span��ʹ�ã����ϲ�
		if (prevSpan->_isUse == true)
		{
			break;
		}

		if (prevSpan->_n + span->_n > NPAGES - 1) // �ϲ�������128ҳ��spanû�취�������ϲ�
		{
			break;
		}

		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;

		_spanLists[prevSpan->_n].Erase(prevSpan);
		//delete prevSpan;
		_spanPool.Delete(prevSpan);
	}

	while (true) // ���ϲ�
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