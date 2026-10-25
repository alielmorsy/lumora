// Raw __lumora bundle: shows "Count: 0", then a 100 ms timer bumps it to 1.
(function () {
    var label = __lumora.createNode("text");
    __lumora.setProp(label, "text", "Count: 0");
    __lumora.insert(__lumora.root(), label, null);
    var fired = false;
    globalThis.__lumoraTick = function (nowMs) {
        if (!fired && nowMs >= 100) {
            fired = true;
            __lumora.setProp(label, "text", "Count: 1");
        }
    };
})();
