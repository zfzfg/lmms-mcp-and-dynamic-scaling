#include <QtTest>
#include <QImage>
#include <QPainter>
#include <QWidget>
#include "SampleThumbnail.h"
using namespace lmms;
namespace
{
class WaveformWidget : public QWidget
{
public:
    gui::SampleThumbnail thumbnail;
    gui::SampleThumbnail::VisualizeParameters parameters;
    explicit WaveformWidget(std::shared_ptr<const SampleBuffer> buffer) : thumbnail(std::move(buffer)) { resize(192, 64); }
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        painter.setPen(Qt::white);
        parameters.sampleRect = rect();
        thumbnail.visualize(parameters, painter);
    }
    QImage image()
    {
        QImage image(size(), QImage::Format_ARGB32);
        image.fill(Qt::black);
        render(&image);
        return image;
    }
};
bool litColumn(const QImage& image, int column)
{
    for (int y = 0; y < image.height(); ++y) { if (qRed(image.pixel(column, y)) > 64) { return true; } }
    return false;
}
}
class SampleThumbnailTest : public QObject
{
    Q_OBJECT
private slots:
    void emptyWidgetRendersSafely()
    {
        WaveformWidget widget(std::make_shared<const SampleBuffer>(std::vector<SampleFrame>{}, 44100));
        const auto image = widget.image();
        for (int x = 0; x < image.width(); ++x) { QVERIFY(!litColumn(image, x)); }
    }
    void stereoTailAndReverse_data()
    {
        QTest::addColumn<int>("frames");
        for (int frames : {1, 3, 101, 10000}) { QTest::newRow(qPrintable(QString::number(frames))) << frames; }
    }
    void stereoTailAndReverse()
    {
        QFETCH(int, frames);
        std::vector<SampleFrame> samples(frames);
        samples.back() = SampleFrame(1.f, -1.f);
        WaveformWidget widget(std::make_shared<const SampleBuffer>(std::move(samples), 44100));
        auto image = widget.image();
        QVERIFY(litColumn(image, image.width() - 1));
        QVERIFY(qRed(image.pixel(image.width() - 1, 8)) > 64);
        QVERIFY(qRed(image.pixel(image.width() - 1, image.height() - 8)) > 64);
        widget.parameters.reversed = true;
        image = widget.image();
        QVERIFY(litColumn(image, 0));
        widget.parameters.reversed = false;
        widget.parameters.sampleStart = 0.5f;
        widget.parameters.sampleEnd = 1.f;
        image = widget.image();
        QVERIFY(litColumn(image, image.width() - 1));
    }
    void revisionSeesEveryFrame()
    {
        std::vector<SampleFrame> samples(10000);
        SampleBuffer before(samples, 44100);
        samples[123] = SampleFrame(1.f, 0.f);
        SampleBuffer after(std::move(samples), 44100);
        QVERIFY(before.contentRevision() != after.contentRevision());
    }
};
QTEST_MAIN(SampleThumbnailTest)
#include "SampleThumbnailTest.moc"
