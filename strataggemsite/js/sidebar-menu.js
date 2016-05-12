window.addEventListener('DOMContentLoaded', function() {
  // detach sidenav from document
  sidenavEl = document.getElementById('sidenav');
  sidenavEl.parentNode.removeChild(sidenavEl);
  function showSidenav() {
    // turn on overlay
    mui.overlay('on', sidenavEl, {
      onclose: function() {sidenavEl.className = '';}
    });
    // animate sidenav
    setTimeout(function() {
      sidenavEl.className = 'sidenav-show';
    }, 0);
  }
  // instrument toggle element
  var toggleEl = document.getElementById('sidenav-toggle');
  toggleEl.addEventListener('click', showSidenav);
});
