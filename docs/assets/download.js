/*
 * Mirab website: point the download buttons at the files of the latest release.
 *
 * Each button names the end of its file name in data-asset, for example
 * "-windows-x64.exe". Without JavaScript, or when GitHub cannot be reached,
 * the buttons keep their link to the latest release page.
 */

(function () {
  "use strict";

  var RELEASE_API = "https://api.github.com/repos/ehsanMa86/Mirab/releases/latest";

  function platformSuffix() {
    var ua = navigator.userAgent;
    if (/Windows/.test(ua)) return "-windows-x64.exe";
    if (/Macintosh|Mac OS X/.test(ua)) return "-macos-arm64.dmg";
    if (/Linux/.test(ua) && !/Android/.test(ua)) return "-linux-x86_64.AppImage";
    return null;
  }

  function update() {
    var buttons = document.querySelectorAll("a[data-asset]");
    if (buttons.length === 0) return;

    var suffix = platformSuffix();
    buttons.forEach(function (button) {
      button.classList.toggle("mirab-recommended", button.getAttribute("data-asset") === suffix);
    });

    fetch(RELEASE_API, { headers: { Accept: "application/vnd.github+json" } })
      .then(function (response) {
        if (!response.ok) throw new Error("GitHub answered " + response.status);
        return response.json();
      })
      .then(function (release) {
        buttons.forEach(function (button) {
          var ending = button.getAttribute("data-asset");
          var asset = release.assets.find(function (a) {
            return a.name.endsWith(ending);
          });
          if (asset) {
            button.href = asset.browser_download_url;
            button.title = asset.name + " (" + (asset.size / 1048576).toFixed(0) + " MB)";
          }
        });
        var note = document.querySelector("[data-mirab-release]");
        if (note) {
          var date = (release.published_at || "").slice(0, 10);
          note.textContent = "";
          note.append("Latest release: ");
          var link = document.createElement("a");
          link.href = release.html_url;
          link.textContent = release.name || release.tag_name;
          note.append(link);
          if (date) note.append(", published " + date + ".");
        }
      })
      .catch(function () {
        // The buttons keep their link to the release page.
      });
  }

  // Material loads pages without a full reload; document$ fires for each one.
  if (window.document$ && typeof window.document$.subscribe === "function") {
    window.document$.subscribe(update);
  } else {
    document.addEventListener("DOMContentLoaded", update);
  }
})();
