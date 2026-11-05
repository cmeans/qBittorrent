/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2026  Chris Means
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 */

#include <QDateTime>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QVariantHash>

#include "base/global.h"
#include "base/rss/rss_article.h"
#include "base/rss/rss_autodownloadrule.h"
#include "base/rss/rss_parser.h"

namespace
{
    const qint64 ONE_MIB = 1048576;
    const qint64 ONE_GIB = 1073741824;
    const qint64 TWO_GIB = 2147483648;
    const qint64 FIVE_GIB = 5368709120;

    QVariantHash article(const QString &description)
    {
        return {{RSS::Article::KeyTitle, u"Some Show S01E01"_s}, {RSS::Article::KeyDescription, description}};
    }

    QVariantHash parseSingleItem(const QString &itemBody)
    {
        const QString feed = u"<?xml version=\"1.0\"?><rss version=\"2.0\"><channel><title>Feed</title>"
                "<item><title>Some Show</title><guid>id-1</guid>%1</item></channel></rss>"_s.arg(itemBody);

        RSS::Private::Parser parser {{}};
        QSignalSpy spy {&parser, &RSS::Private::Parser::finished};
        parser.parse(feed.toUtf8());

        if (spy.count() != 1)
            return {};
        const auto result = spy.at(0).at(0).value<RSS::Private::ParsingResult>();
        return result.articles.isEmpty() ? QVariantHash {} : result.articles.first();
    }
}

class TestRSSAutoDownloadRule final : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(TestRSSAutoDownloadRule)

public:
    TestRSSAutoDownloadRule() = default;

private slots:
    void initTestCase() const
    {
        qRegisterMetaType<RSS::Private::ParsingResult>();
    }

    void testMatchesSizeFromDescription_data() const
    {
        QTest::addColumn<qint64>("minSize");
        QTest::addColumn<qint64>("maxSize");
        QTest::addColumn<QString>("description");
        QTest::addColumn<bool>("expected");

        // Descriptions in the first group have the format of a real tracker feed
        QTest::newRow("no limits accept any size") << qint64(0) << qint64(0) << u"Movies / SD / 500 GiB"_s << true;
        QTest::newRow("under max") << qint64(0) << TWO_GIB << u"Factual / HD1080p / 1.7 GiB\n                         "_s << true;
        QTest::newRow("over max") << qint64(0) << TWO_GIB << u"Movies / SD / 2.5 GiB"_s << false;
        QTest::newRow("under min") << ONE_GIB << qint64(0) << u"Entertainment / SD / 903.21 MiB\n   / Freeleech!  "_s << false;
        QTest::newRow("over min") << ONE_GIB << qint64(0) << u"Movies / SD / 1.5 GiB"_s << true;
        QTest::newRow("inside range") << ONE_GIB << TWO_GIB << u"Movies / SD / 1.5 GiB"_s << true;

        QTest::newRow("max is inclusive") << qint64(0) << TWO_GIB << u"2 GiB"_s << true;
        QTest::newRow("min is inclusive") << TWO_GIB << qint64(0) << u"2 GiB"_s << true;
        QTest::newRow("just over max") << qint64(0) << TWO_GIB << u"2.01 GiB"_s << false;

        QTest::newRow("GB is 1000-based") << qint64(0) << qint64(2000000000) << u"2 GB"_s << true;
        QTest::newRow("GiB is 1024-based") << qint64(0) << qint64(2000000000) << u"2 GiB"_s << false;
        QTest::newRow("MiB unit") << qint64(0) << (100 * ONE_MIB) << u"101 MiB"_s << false;
        QTest::newRow("units ignore case") << qint64(0) << qint64(700000000) << u"701 mb"_s << false;
        QTest::newRow("TiB unit") << qint64(0) << FIVE_GIB << u"1 TiB"_s << false;

        QTest::newRow("thousands separator, at limit") << qint64(0) << ONE_GIB << u"1,024 MiB"_s << true;
        QTest::newRow("thousands separator, over limit") << qint64(0) << (ONE_GIB - 1) << u"1,024 MiB"_s << false;
        QTest::newRow("decimal comma, at limit") << qint64(0) << qint64(1610612736) << u"1,5 GiB"_s << true;
        QTest::newRow("decimal comma, over limit") << qint64(0) << qint64(1610612735) << u"1,5 GiB"_s << false;
        QTest::newRow("thousands and decimals") << qint64(0) << (1025 * ONE_MIB) << u"1,024.5 MiB"_s << true;
        QTest::newRow("thousands and decimals, over limit") << qint64(0) << (1024 * ONE_MIB) << u"1,024.5 MiB"_s << false;
        QTest::newRow("decimal comma after four digits") << qint64(0) << (1025 * ONE_MIB) << u"1024,500 MiB"_s << true;

        // Feeds can separate the number and the unit with something other than a plain space
        QTest::newRow("no-break space before unit") << qint64(0) << ONE_GIB << u"1.71 GiB"_s << false;
        QTest::newRow("thin space before unit") << qint64(0) << ONE_GIB << u"1.71 GiB"_s << false;
        QTest::newRow("HTML no-break space before unit") << qint64(0) << ONE_GIB << u"Size: 1.71&nbsp;GiB"_s << false;
        QTest::newRow("HTML numeric no-break space before unit") << qint64(0) << ONE_GIB << u"Size: 1.71&#160;GiB"_s << false;
        QTest::newRow("HTML hexadecimal no-break space before unit") << qint64(0) << ONE_GIB << u"Size: 1.71&#xA0;GiB"_s << false;
        QTest::newRow("HTML thin space before unit") << qint64(0) << ONE_GIB << u"Size: 1.71&thinsp;GiB"_s << false;
        QTest::newRow("HTML numeric thin space before unit") << qint64(0) << ONE_GIB << u"Size: 1.71&#8201;GiB"_s << false;

        // Similar to the unreadable text below, but these are sizes and must keep working
        QTest::newRow("slash right after unit") << qint64(0) << ONE_GIB << u"Movies / 1.7 GiB/SD"_s << false;
        QTest::newRow("hyphen after word") << qint64(0) << ONE_GIB << u"HD-1.7 GiB"_s << false;
        QTest::newRow("spaced hyphen after number") << qint64(0) << ONE_GIB << u"Season 2 - 5 GiB"_s << false;

        // The size is unknown in these, so the limit (which they would otherwise fail) doesn't apply
        QTest::newRow("empty description") << qint64(0) << qint64(1) << QString() << true;
        QTest::newRow("no size in description") << qint64(0) << qint64(1) << u"Movies / SD"_s << true;
        QTest::newRow("several sizes in description") << qint64(0) << qint64(1) << u"1 GiB of 5 GiB"_s << true;
        QTest::newRow("a bare B is not a size") << qint64(0) << qint64(1) << u"Series 4 B"_s << true;
        QTest::newRow("number without unit") << qint64(0) << qint64(1) << u"Movies / 1080"_s << true;
        QTest::newRow("size range with hyphen") << qint64(0) << qint64(1) << u"1-2 GiB"_s << true;
        QTest::newRow("size range with en dash") << qint64(0) << qint64(1) << u"1–2 GiB"_s << true;
        QTest::newRow("transfer rate") << qint64(0) << qint64(1) << u"Speed 10 MB/s"_s << true;
        QTest::newRow("transfer rate with sec") << qint64(0) << qint64(1) << u"Speed 10 MiB/sec"_s << true;
    }

    void testMatchesSizeFromDescription() const
    {
        QFETCH(const qint64, minSize);
        QFETCH(const qint64, maxSize);
        QFETCH(const QString, description);
        QFETCH(const bool, expected);

        RSS::AutoDownloadRule rule {u"rule"_s};
        rule.setMinSize(minSize);
        rule.setMaxSize(maxSize);

        QCOMPARE(rule.matches(article(description)), expected);
    }

    void testSizeSourcePrecedence_data() const
    {
        QTest::addColumn<QVariant>("torrentSize");
        QTest::addColumn<QVariant>("sizeElement");
        QTest::addColumn<QString>("description");
        QTest::addColumn<bool>("expected");

        // Each row is checked against a 2 GiB maximum
        const QVariant none;
        QTest::newRow("enclosure length beats size element") << QVariant(FIVE_GIB) << QVariant(u"1073741824"_s) << QString() << false;
        QTest::newRow("enclosure length beats description") << QVariant(ONE_GIB) << none << u"5 GiB"_s << true;
        QTest::newRow("size element in bytes beats description") << none << QVariant(u"1073741824"_s) << u"5 GiB"_s << true;
        QTest::newRow("size element in bytes over limit") << none << QVariant(u"5368709120"_s) << u"1 GiB"_s << false;
        QTest::newRow("size element with unit") << none << QVariant(u"5 GiB"_s) << u"1 GiB"_s << false;
        QTest::newRow("unreadable size element falls back") << none << QVariant(u"unknown"_s) << u"5 GiB"_s << false;
        QTest::newRow("zero size element falls back") << none << QVariant(u"0"_s) << u"5 GiB"_s << false;
        QTest::newRow("zero enclosure length falls back") << QVariant(qint64(0)) << none << u"5 GiB"_s << false;
    }

    void testSizeSourcePrecedence() const
    {
        QFETCH(const QVariant, torrentSize);
        QFETCH(const QVariant, sizeElement);
        QFETCH(const QString, description);
        QFETCH(const bool, expected);

        RSS::AutoDownloadRule rule {u"rule"_s};
        rule.setMaxSize(TWO_GIB);

        QVariantHash data = article(description);
        if (torrentSize.isValid())
            data[RSS::Article::KeyTorrentSize] = torrentSize;
        if (sizeElement.isValid())
            data[u"size"_s] = sizeElement;

        QCOMPARE(rule.matches(data), expected);
    }

    void testEnclosureLengthSurvivesStorage() const
    {
        // Articles are stored as JSON, so a saved length comes back as a double
        RSS::AutoDownloadRule rule {u"rule"_s};
        rule.setMaxSize(TWO_GIB);

        QVariantHash data = article({});
        data[RSS::Article::KeyTorrentSize] = QVariant(double(FIVE_GIB));

        QVERIFY(!rule.matches(data));
    }

    void testOtherFiltersStillApply() const
    {
        RSS::AutoDownloadRule rule {u"rule"_s};
        rule.setMaxSize(TWO_GIB);
        rule.setMustContain(u"Other Show"_s);

        QVERIFY(!rule.matches(article(u"1 GiB"_s)));
        QVERIFY(!rule.matches(article(u"Movies / SD"_s)));
    }

    void testRejectedSizeIsNotRecordedAsMatch() const
    {
        RSS::AutoDownloadRule rule {u"rule"_s};
        rule.setMaxSize(TWO_GIB);

        QVariantHash data = article(u"5 GiB"_s);
        data[RSS::Article::KeyDate] = QDateTime::fromString(u"2026-10-19T15:35:33Z"_s, Qt::ISODate);

        QVERIFY(!rule.accepts(data));
        QVERIFY(!rule.lastMatch().isValid());
    }

    void testSizeLimitsSurviveJsonRoundTrip() const
    {
        RSS::AutoDownloadRule rule {u"rule"_s};
        rule.setMinSize(ONE_GIB);
        rule.setMaxSize(FIVE_GIB);

        const QJsonObject json = rule.toJsonObject();
        QCOMPARE(json.value(u"minSize"_s).toInteger(), ONE_GIB);
        QCOMPARE(json.value(u"maxSize"_s).toInteger(), FIVE_GIB);

        const RSS::AutoDownloadRule loaded = RSS::AutoDownloadRule::fromJsonObject(json, u"rule"_s);
        QCOMPARE(loaded.minSize(), ONE_GIB);
        QCOMPARE(loaded.maxSize(), FIVE_GIB);
    }

    void testRuleWithoutSizeLimitsLoads() const
    {
        const QJsonObject oldRule {{u"mustContain"_s, u"Show"_s}};
        const RSS::AutoDownloadRule loaded = RSS::AutoDownloadRule::fromJsonObject(oldRule, u"rule"_s);

        QCOMPARE(loaded.minSize(), qint64(0));
        QCOMPARE(loaded.maxSize(), qint64(0));
        QVERIFY(loaded.matches(article(u"500 GiB"_s)));
    }

    void testNegativeLimitsMeanNoLimit() const
    {
        RSS::AutoDownloadRule rule {u"rule"_s};
        rule.setMinSize(-1);
        rule.setMaxSize(-1);

        QCOMPARE(rule.minSize(), qint64(0));
        QCOMPARE(rule.maxSize(), qint64(0));
        QVERIFY(rule.matches(article(u"1 GiB"_s)));
    }

    void testSizeLimitsAffectRuleEquality() const
    {
        RSS::AutoDownloadRule left {u"rule"_s};
        RSS::AutoDownloadRule right {u"rule"_s};
        QVERIFY(left == right);

        right.setMaxSize(TWO_GIB);
        QVERIFY(!(left == right));

        right.setMaxSize(0);
        right.setMinSize(ONE_GIB);
        QVERIFY(!(left == right));
    }

    void testParserReadsEnclosureLength_data() const
    {
        QTest::addColumn<QString>("itemBody");
        QTest::addColumn<qint64>("expected");

        QTest::newRow("torrent enclosure")
            << u"<enclosure url=\"http://x/a.torrent\" type=\"application/x-bittorrent\" length=\"5368709120\"/>"_s
            << FIVE_GIB;
        QTest::newRow("untyped enclosure")
            << u"<enclosure url=\"http://x/a.torrent\" length=\"1073741824\"/>"_s
            << ONE_GIB;
        QTest::newRow("torrent enclosure beats untyped")
            << u"<enclosure url=\"http://x/b\" length=\"1073741824\"/>"
                "<enclosure url=\"http://x/a.torrent\" type=\"application/x-bittorrent\" length=\"5368709120\"/>"_s
            << FIVE_GIB;
        QTest::newRow("other enclosure types are not the torrent")
            << u"<enclosure url=\"http://x/a.mp3\" type=\"audio/mpeg\" length=\"1073741824\"/>"_s
            << qint64(-1);
        QTest::newRow("zero length")
            << u"<enclosure url=\"http://x/a.torrent\" type=\"application/x-bittorrent\" length=\"0\"/>"_s
            << qint64(-1);
        QTest::newRow("unreadable length")
            << u"<enclosure url=\"http://x/a.torrent\" type=\"application/x-bittorrent\" length=\"big\"/>"_s
            << qint64(-1);
        QTest::newRow("no length")
            << u"<enclosure url=\"http://x/a.torrent\" type=\"application/x-bittorrent\"/>"_s
            << qint64(-1);
    }

    void testParserReadsEnclosureLength() const
    {
        QFETCH(const QString, itemBody);
        QFETCH(const qint64, expected);

        const QVariantHash data = parseSingleItem(itemBody);
        QVERIFY(!data.isEmpty());

        const QVariant size = data.value(RSS::Article::KeyTorrentSize);
        QCOMPARE((size.isValid() ? size.toLongLong() : qint64(-1)), expected);
    }

    void testFilterUsesParsedEnclosureLength() const
    {
        RSS::AutoDownloadRule rule {u"rule"_s};
        rule.setMaxSize(TWO_GIB);

        const QVariantHash data = parseSingleItem(u"<description>1 GiB</description>"
                "<enclosure url=\"http://x/a.torrent\" type=\"application/x-bittorrent\" length=\"5368709120\"/>"_s);

        QVERIFY(!rule.matches(data));
    }

    void testFilterUsesParsedSizeElement() const
    {
        RSS::AutoDownloadRule rule {u"rule"_s};
        rule.setMaxSize(TWO_GIB);

        QVERIFY(!rule.matches(parseSingleItem(u"<size>5368709120</size>"_s)));
    }
};

QTEST_APPLESS_MAIN(TestRSSAutoDownloadRule)
#include "testrssautodownloadrule.moc"
