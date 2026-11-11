// Apply saved theme before paint (no flash)
(function () {
  var t = null;
  try {
    t = localStorage.getItem("agrisense_theme");
  } catch (e) {}
  if (!t)
    t = matchMedia("(prefers-color-scheme: dark)").matches
      ? "dark"
      : "light";
  document.documentElement.setAttribute("data-theme", t);
})();
