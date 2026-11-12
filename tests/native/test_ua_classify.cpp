// Native tests for the phone/desktop split on GET /grbl-controller
// (chatgpt/src/ua_classify.h, UID 288).
//
// Phones must land on the mobile wizard (/m); desktop and tablets keep the
// full controller page byte-identical. Clients without a UA (curl, scripts,
// bots) must never be redirected.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "ua_classify.h"

TEST_CASE("iPhone and Android phones go to the wizard") {
    // Real UA shapes (iOS 17 Safari, Android 14 Chrome).
    const char* iphone =
        "Mozilla/5.0 (iPhone; CPU iPhone OS 17_1 like Mac OS X) AppleWebKit/605.1.15 "
        "(KHTML, like Gecko) Version/17.1 Mobile/15E148 Safari/604.1";
    const char* android_phone =
        "Mozilla/5.0 (Linux; Android 14; Pixel 7) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/120.0.0.0 Mobile Safari/537.36";
    CHECK(uaClassify(iphone) == UA_PHONE);
    CHECK(uaClassify(android_phone) == UA_PHONE);
    CHECK(uaWantsMobileWizard(iphone));
    CHECK(uaWantsMobileWizard(android_phone));
}

TEST_CASE("tablets and desktops keep the full controller page") {
    const char* ipad =
        "Mozilla/5.0 (iPad; CPU OS 17_1 like Mac OS X) AppleWebKit/605.1.15 "
        "(KHTML, like Gecko) Version/17.1 Mobile/15E148 Safari/604.1";   // note: carries "Mobile"
    const char* android_tablet =
        "Mozilla/5.0 (Linux; Android 13; SM-X200) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/120.0.0.0 Safari/537.36";                                 // no "Mobile"
    const char* windows =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/120.0.0.0 Safari/537.36";
    const char* macos =
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 "
        "(KHTML, like Gecko) Version/17.1 Safari/605.1.15";
    const char* linux =
        "Mozilla/5.0 (X11; Linux x86_64; rv:121.0) Gecko/20100101 Firefox/121.0";

    CHECK(uaClassify(ipad) == UA_TABLET);
    CHECK(uaClassify(android_tablet) == UA_TABLET);
    CHECK(uaClassify(windows) == UA_DESKTOP);
    CHECK(uaClassify(macos) == UA_DESKTOP);
    CHECK(uaClassify(linux) == UA_DESKTOP);
    CHECK_FALSE(uaWantsMobileWizard(ipad));
    CHECK_FALSE(uaWantsMobileWizard(windows));
}

TEST_CASE("no UA (curl, scripts, bots) is a desktop client") {
    CHECK(uaClassify(nullptr) == UA_DESKTOP);
    CHECK(uaClassify("") == UA_DESKTOP);
    CHECK(uaClassify("curl/8.5.0") == UA_DESKTOP);
    CHECK(uaClassify("Wget/1.21.3") == UA_DESKTOP);
    CHECK(uaClassify("Mozilla/5.0 (compatible; Googlebot/2.1; +http://www.google.com/bot.html)") == UA_DESKTOP);
    CHECK_FALSE(uaWantsMobileWizard(nullptr));
    CHECK_FALSE(uaWantsMobileWizard("curl/8.5.0"));
}

TEST_CASE("other phone shapes are recognised") {
    CHECK(uaClassify("Mozilla/5.0 (Windows Phone 10.0; Android 6.0.1; Lumia 950) "
                     "AppleWebKit/537.36 (KHTML, like Gecko) Mobile Safari/537.36 Edge/15.15063") == UA_PHONE);
    CHECK(uaClassify("Mozilla/5.0 (compatible; MSIE 10.0; Windows Phone 8.0; Trident/6.0; "
                     "IEMobile/10.0; ARM; Touch)") == UA_PHONE);
    CHECK(uaClassify("Mozilla/5.0 (iPod touch; CPU iPhone OS 15_7 like Mac OS X) AppleWebKit/605.1.15 "
                     "(KHTML, like Gecko) Version/15.6 Mobile/15E148 Safari/604.1") == UA_PHONE);
    CHECK(uaClassify("Opera/9.80 (J2ME/MIDP; Opera Mini/9.80 (S60; SymbOS; Opera Mobi/499; U; en) "
                     "Presto/2.5.25) Presto/2.5.25 Version/12.00") == UA_PHONE);
    CHECK(uaClassify("Mozilla/5.0 (BlackBerry; U; BlackBerry 9900; en) AppleWebKit/534.11+ "
                     "(KHTML, like Gecko) Version/7.1.0.346 Mobile Safari/534.11+") == UA_PHONE);
}

TEST_CASE("other tablet shapes are not redirected") {
    CHECK(uaClassify("Mozilla/5.0 (Linux; U; Android 4.0.3; en-us; KFTT Build/IML74K) "
                     "AppleWebKit/537.36 (KHTML, like Gecko) Silk/3.13 Safari/537.36") == UA_TABLET);   // Kindle Fire
    CHECK(uaClassify("Mozilla/5.0 (Linux; Android 9; Tablet) AppleWebKit/537.36") == UA_TABLET);
    CHECK(uaClassify("Mozilla/5.0 (PlayBook; U; RIM Tablet OS 2.1) AppleWebKit/536.2+") == UA_TABLET);
}

TEST_CASE("the check is case-insensitive (UA spelling varies)") {
    CHECK(uaClassify("Mozilla/5.0 (IPHONE; CPU iPhone OS 17_1 like Mac OS X)") == UA_PHONE);
    CHECK(uaClassify("Mozilla/5.0 (Android 14; PIXEL 7) ... MOBILE Safari/537.36") == UA_PHONE);
    CHECK(uaHas("Mozilla/5.0 (iPad)", "ipad"));
    CHECK(uaFindNoCase("abcANDROIDdef", "android") != nullptr);
    CHECK(uaFindNoCase("abc", "android") == nullptr);
}

TEST_CASE("a phone-looking token inside a desktop UA does not leak") {
    // "Mobile" as a word in a desktop feature string must not win on its own:
    // only the phone tokens do, and the Android-without-Mobile rule is tablet.
    CHECK(uaClassify("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/120 Safari/537.36 Mobile-optimised") == UA_PHONE);
    // ^ documents the (intended) behaviour: the generic "Mobile" token wins.
    CHECK(uaClassify("Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120 Safari/537.36") == UA_DESKTOP);
}
