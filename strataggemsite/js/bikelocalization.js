var markers = [];
var map;


function initMap() {
    map = new google.maps.Map(document.getElementById('googleMap'), {
      zoom: 2,
      center: {lat: 30.0, lng: 0.0}
    });
	var opt = { minZoom: 2, maxZoom: 0 };
	map.setOptions(opt);
  }

  function dropLocation(gps) {
    clearMarkers();
    addMarkerWithTimeout(gps, 200);
	map.setZoom(14);
	map.setCenter(gps);

  }

  function addMarkerWithTimeout(position, timeout) {
    window.setTimeout(function() {
      markers.push(new google.maps.Marker({
        position: position,
        map: map,
        icon:'img/bikeicon.png',
        animation: google.maps.Animation.DROP
      }));
    }, timeout);
  }

  function clearMarkers() {
    for (var i = 0; i < markers.length; i++) {
      markers[i].setMap(null);
    }
    markers = [];
  }

  google.maps.event.addDomListener(window, 'load', initMap); 
