var traffic = {
  onload: function(e) {
    this.init = true;
    dump("Hello\n");
    var nsCommandLine = window.arguments[0];
    nsCommandLine = nsCommandLine.QueryInterface(Components.interfaces.nsICommandLine);
    this.url = nsCommandLine.handleFlagWithParam("url", false);
    this.url = this.url || "http://www.google.com";

    this.title = nsCommandLine.handleFlagWithParam("title", false);

    document.getElementById("main-window").setAttribute("title", this.title);

    this.browser = document.getElementById("main-browser");
    this.browser.loadURI(this.url, null, null);
  },
};

window.addEventListener("load", traffic.onload, false);
