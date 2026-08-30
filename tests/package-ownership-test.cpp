#include <QtCore>

#include "general.h"
#include "package.h"

#include <cstdio>

namespace {

struct ExpectedMigration
{
    const char *general;
    const char *targetFactory;
    const char *targetPackage;
    const char *sourceFactory;
};

struct ExpectedGiteeGeneral
{
    const char *general;
    const char *factory;
    const char *package;
};

const ExpectedMigration expectedMigrations[] = {
    { "nos_caoren", "NostalgiaWind", "nostal_wind", "Wind" },
    { "nos_zhoutai", "NostalgiaWind", "nostal_wind", "Wind" },
    { "nos_zhangjiao", "NostalgiaWind", "nostal_wind", "Wind" },
    { "nos_yuji", "NostalgiaWind", "nostal_wind", "Wind" },
    { "ol_shenguanyu", "OLStWind", "ol_st_wind", "Wind" },
    { "tenyear_zhangjiao", "TenyearStWind", "tenyear_st_wind", "Wind" },
    { "nos_fazheng", "NostalgiaYJCM", "nostal_yjcm", "YJCM" },
    { "nos_lingtong", "NostalgiaYJCM", "nostal_yjcm", "YJCM" },
    { "nos_xushu", "NostalgiaYJCM", "nostal_yjcm", "YJCM" },
    { "nos_zhangchunhua", "NostalgiaYJCM", "nostal_yjcm", "YJCM" },
    { "nos_zhonghui", "NostalgiaYJCM", "nostal_yjcm", "YJCM" },
    { "ol_caozhi", "OLStYJ2011", "ol_st_yj2011", "YJCM" },
    { "ol_masu", "OLStYJ2011", "ol_st_yj2011", "YJCM" },
    { "ol_xusheng", "OLStYJ2011", "ol_st_yj2011", "YJCM" },
    { "ol_yujin", "OLStYJ2011", "ol_st_yj2011", "YJCM" },
    { "mobile_masu", "MobileStYJ2011", "mobile_st_yj2011", "YJCM" },
    { "nos_guanxingzhangbao", "NostalgiaYJCM2012", "nostal_yjcm2012", "YJCM2012" },
    { "nos_handang", "NostalgiaYJCM2012", "nostal_yjcm2012", "YJCM2012" },
    { "nos_madai", "NostalgiaYJCM2012", "nostal_yjcm2012", "YJCM2012" },
    { "nos_wangyi", "NostalgiaYJCM2012", "nostal_yjcm2012", "YJCM2012" },
    { "new_liubiao", "NewYJCM2012", "NewYJCM2012", "YJCM2012" },
    { "second_wangyi", "SecondYJCM2012", "SecondYJCM2012", "YJCM2012" },
    { "ol_bulianshi", "OLStYJ2012", "ol_st_yj2012", "YJCM2012" },
    { "ol_liubiao", "OLStYJ2012", "ol_st_yj2012", "YJCM2012" },
    { "ol_madai", "OLStYJ2012", "ol_st_yj2012", "YJCM2012" },
    { "ol_wangyi", "OLStYJ2012", "ol_st_yj2012", "YJCM2012" },
    { "nos_caochong", "NostalgiaYJCM2013", "nostal_yjcm2013", "YJCM2013" },
    { "nos_fuhuanghou", "NostalgiaYJCM2013", "nostal_yjcm2013", "YJCM2013" },
    { "nos_liru", "NostalgiaYJCM2013", "nostal_yjcm2013", "YJCM2013" },
    { "nos_zhuran", "NostalgiaYJCM2013", "nostal_yjcm2013", "YJCM2013" },
    { "ol_guohuai", "OLStYJ2013", "ol_st_yj2013", "YJCM2013" },
    { "new_caozhen", "NewYJCM2014", "NewYJCM2014", "YJCM2014" },
    { "new_chenqun", "NewYJCM2014", "NewYJCM2014", "YJCM2014" },
    { "new_zhoucang", "NewYJCM2014", "NewYJCM2014", "YJCM2014" },
    { "new_zhuhuan", "NewYJCM2014", "NewYJCM2014", "YJCM2014" },
    { "ol_wuyi", "OLStYJ2014", "ol_st_yj2014", "YJCM2014" },
    { "ol_liuchen", "OLStYJ2015", "OLStYJ2015", "YJCM2015" },
    { "ol_sunxiu", "OLStYJ2015", "OLStYJ2015", "YJCM2015" },
    { "new_caorui", "NewYJCM2015", "NewYJCM2015", "YJCM2015" },
    { "new_caoxiu", "NewYJCM2015", "NewYJCM2015", "YJCM2015" },
    { "new_quancong", "NewYJCM2015", "NewYJCM2015", "YJCM2015" },
    { "new_zhuzhi", "NewYJCM2015", "NewYJCM2015", "YJCM2015" },
    { "ol_sunliang", "OLStYin", "OLStYin", "Yin" },
    { "ol_luzhi", "OLStYin", "OLStYin", "Yin" },
    { "ol_guanqiujian", "OLStLei", "OLStLei", "Lei" },
    { "ol_shenzhangliao", "OLStLei", "OLStLei", "Lei" },
    { "mobile_guanqiujian", "MobileStLei", "MobileStLei", "Lei" },
    { "ol_xinxianying", "OLStYC2017", "OLStYC2017", "YCZH2017" },
    { "mobile_jikang", "MobileStYC2017", "MobileStYC2017", "YCZH2017" },
    { "tenyear_pangde", "TenyearStFire", "TenyearStFire", "Fire" },
    { "tenyear_xuhuang", "TenyearStThicket", "TenyearStThicket", "Thicket" },
    { "new_shencaocao", "NewShen", "NewShen", "Thicket" },
    { "new_shenzhaoyun", "NewShen", "NewShen", "Mountain" },
    { "mobile_shensimayi", "MobileStMountain", "mobile_st_mountain", "Mountain" }
};

const ExpectedGiteeGeneral expectedGiteeGenerals[] = {
    { "if_caocao", "Dream", "qs_dream" },
    { "if_caopi", "Dream", "qs_dream" },
    { "if_fazheng", "Dream", "qs_dream" },
    { "if_guanyu", "Dream", "qs_dream" },
    { "if_liubei", "Dream", "qs_dream" },
    { "if_liuhong", "Dream", "qs_dream" },
    { "if_liushan", "Dream", "qs_dream" },
    { "if_liuxie", "Dream", "qs_dream" },
    { "if_lvbu", "Dream", "qs_dream" },
    { "if_pangtong", "Dream", "qs_dream" },
    { "if_zhangjiao", "Dream", "qs_dream" },
    { "if_zhouyu", "Dream", "qs_dream" },
    { "mobile_luyu", "mobile", "mobile" },
    { "xing_wanglang", "mobileStar", "mobile_star" },
    { "mobile_guanyinping", "mobileSp", "mobile_sp" },
    { "mobile_lingju", "mobileSp", "mobile_sp" },
    { "yong_shenmachao", "MobileYong", "mobileyong" },
    { "dongyuan", "OLCcxh", "ol_ccxh" },
    { "ol_cuiyuan", "OLCcxh", "ol_ccxh" },
    { "ol_liuye", "OLCcxh", "ol_ccxh" },
    { "olmo_zhangfei", "OLDemon", "ol_demon" },
    { "olmou_chengyu", "OLMou", "ol_mou" },
    { "olmou_dongzhao", "OLMou", "ol_mou" },
    { "olmou_jiaxu", "OLMou", "ol_mou" },
    { "oljie_yujin", "OLStYJ2011", "ol_st_yj2011" },
    { "oljie_guanping", "OLStYJ2013", "ol_st_yj2013" },
    { "oljie_zhangsong", "OLStYJ2014", "ol_st_yj2014" },
    { "lingju", "SP", "sp" },
    { "th_shencaopi", "TenyearXd", "tenyear_xd" },
    { "thxing_zhanghe", "TenyearXh", "tenyear_xh" },
    { "thxing_zhangsong", "TenyearXh", "tenyear_xh" },
    { "thmou_chunyuqiong", "TenyearMou", "tenyear_mou" },
    { "thmou_dengai", "TenyearMou", "tenyear_mou" },
    { "thmou_fazheng", "TenyearMou", "tenyear_mou" },
    { "thmou_jianggan", "TenyearMou", "tenyear_mou" },
    { "thmou_liuzhang", "TenyearMou", "tenyear_mou" },
    { "thmou_pangtong", "TenyearMou", "tenyear_mou" },
    { "thmou_wuyi", "TenyearMou", "tenyear_mou" },
    { "thmou_xuyou", "TenyearMou", "tenyear_mou" },
    { "thwei_gongsunzan", "TenyearWei", "tenyear_wei" },
    { "thwei_liubei", "TenyearWei", "tenyear_wei" },
    { "caobao", "TenyearHc", "tenyear_hc" },
    { "gongsunxiu", "TenyearHc", "tenyear_hc" },
    { "guanyue", "TenyearHc", "tenyear_hc" },
    { "houzhaoning", "TenyearHc", "tenyear_hc" },
    { "kuaiqi", "TenyearHc", "tenyear_hc" },
    { "liuchongluojun", "TenyearHc", "tenyear_hc" },
    { "liuxuan", "TenyearHc", "tenyear_hc" },
    { "liuyijun", "TenyearHc", "tenyear_hc" },
    { "mengyou", "TenyearHc", "tenyear_hc" },
    { "renwan", "TenyearHc", "tenyear_hc" },
    { "sanciyuan", "TenyearHc", "tenyear_hc" },
    { "sunyu", "TenyearHc", "tenyear_hc" },
    { "taoheng", "TenyearHc", "tenyear_hc" },
    { "th_chengyu", "TenyearHc", "tenyear_hc" },
    { "th_cuiyuanmaojie", "TenyearHc", "tenyear_hc" },
    { "th_huanghao", "TenyearHc", "tenyear_hc" },
    { "th_lvfan", "TenyearHc", "tenyear_hc" },
    { "th_sunziliufang", "TenyearHc", "tenyear_hc" },
    { "th_yanxiang", "TenyearHc", "tenyear_hc" },
    { "th_zangba", "TenyearHc", "tenyear_hc" },
    { "th_zhangshiping", "TenyearHc", "tenyear_hc" },
    { "weifeng", "TenyearHc", "tenyear_hc" },
    { "yue_caozhi", "TenyearHc", "tenyear_hc" },
    { "yue_daqiao", "TenyearHc", "tenyear_hc" },
    { "zhangyu", "TenyearHc", "tenyear_hc" },
    { "oljie_xiahoushi", "OLStYJ2015", "OLStYJ2015" },
    { "xukun", "YJCM2023", "yjcm2023" }
};

Package *packageForFactory(const QString &factoryName,
                           QHash<QString, Package *> &packages)
{
    auto existing = packages.find(factoryName);
    if (existing != packages.end())
        return existing.value();

    const PackageFactory factory = PackageAdder::packages().value(factoryName, nullptr);
    if (!factory)
        return nullptr;
    Package *package = factory();
    packages.insert(factoryName, package);
    return package;
}

}

int runMigratedGeneralPackageTests()
{
    QHash<QString, Package *> packages;
    int result = 0;
    for (const ExpectedMigration &entry : expectedMigrations) {
        Package *target = packageForFactory(QString::fromLatin1(entry.targetFactory), packages);
        Package *source = packageForFactory(QString::fromLatin1(entry.sourceFactory), packages);
        const QString generalName = QString::fromLatin1(entry.general);
        if (!target || target->objectName() != QString::fromLatin1(entry.targetPackage)
            || !target->findChild<const General *>(generalName, Qt::FindDirectChildrenOnly)) {
            std::fprintf(stderr, "target package mismatch for %s\n", entry.general);
            result = 1;
            break;
        }
        if (!source || source->findChild<const General *>(generalName,
                                                          Qt::FindDirectChildrenOnly)) {
            std::fprintf(stderr, "source package still owns %s\n", entry.general);
            result = 2;
            break;
        }
    }
    if (result == 0) {
        for (const ExpectedGiteeGeneral &entry : expectedGiteeGenerals) {
            Package *package = packageForFactory(QString::fromLatin1(entry.factory), packages);
            const QString generalName = QString::fromLatin1(entry.general);
            if (!package || package->objectName() != QString::fromLatin1(entry.package)
                || !package->findChild<const General *>(generalName,
                                                        Qt::FindDirectChildrenOnly)) {
                std::fprintf(stderr, "Gitee general package mismatch for %s\n", entry.general);
                result = 3;
                break;
            }
        }
    }
    qDeleteAll(packages);
    return result;
}
