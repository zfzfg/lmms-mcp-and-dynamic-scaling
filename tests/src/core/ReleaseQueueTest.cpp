#include <algorithm>

#include <QtTest>

#include "NoteTimeIndex.h"
#include "PlayHandleReleaseQueue.h"

using namespace lmms;

class ReleaseQueueTest : public QObject
{
	Q_OBJECT
private slots:
	void fullQueueKeepsTheRejectedPointer()
	{
		BoundedPointerQueue<int, 4> queue;
		int values[4] = {1, 2, 3, 4};
		QVERIFY(queue.tryPush(&values[0]));
		QVERIFY(queue.tryPush(&values[1]));
		QVERIFY(queue.tryPush(&values[2]));
		QVERIFY(!queue.tryPush(&values[3]));
		QCOMPARE(queue.tryPop(), &values[0]);
		QVERIFY(queue.tryPush(&values[3]));
		QCOMPARE(queue.tryPop(), &values[1]);
		QCOMPARE(queue.tryPop(), &values[2]);
		QCOMPARE(queue.tryPop(), &values[3]);
		QCOMPARE(queue.tryPop(), nullptr);
	}

	void negativeDetuneExtendsTheSpanStart()
	{
		int start = 0;
		int end = 0;
		NoteTimeIndex::spanBounds(1000, 10, -50, 80, 0, &start, &end);
		QCOMPARE(start, 950);
		QCOMPARE(end, 1080);
		NoteTimeIndex::spanBounds(10, -3, 0, 0, 0, &start, &end);
		QCOMPARE(start, 10);
		QCOMPARE(end, 14);
		NoteTimeIndex::spanBounds(0, 0, 0, 0, 0, &start, &end);
		QCOMPARE(start, 0);
		QCOMPARE(end, 0);

		auto linear = [](const std::vector<NoteTimeIndex::Span>& spans, int rangeStart, int rangeEnd) {
			std::vector<int> found;
			for (const auto& span : spans)
			{
				if (span.start <= rangeEnd && span.end >= rangeStart) { found.push_back(span.noteIndex); }
			}
			std::sort(found.begin(), found.end());
			return found;
		};
		const std::vector<NoteTimeIndex::Span> spans{
			{950, 1080, 0},
			{5000, 5200, 1},
		};
		NoteTimeIndex index;
		index.rebuild(spans);
		std::vector<int> indexed;
		index.query(960, 970, indexed);
		std::sort(indexed.begin(), indexed.end());
		const auto oracle = linear(spans, 960, 970);
		QCOMPARE(indexed.size(), oracle.size());
		QCOMPARE(indexed[0], oracle[0]);
		index.query(900, 940, indexed);
		QVERIFY(indexed.empty());
	}
};

QTEST_GUILESS_MAIN(ReleaseQueueTest)
#include "ReleaseQueueTest.moc"
