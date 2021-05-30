var note = document.createElement("div");
note.style = "position:fixed;left:0;bottom:0;display:table;padding:20px;background:#fff;font-size:30px;z-index:2";
document.body.appendChild(note);


/**
 *  wayData, dataOrdering as derived from coordinates documentation and constructor cases
 */
function AutoFollow(wayData, dataOrdering) {
  /* implementation detail variables */
  var coordinates;                                  // array of latLng, contract: the coordinate immediately following a coordinate which is the start coordinate of a section, is the end coordinate of this section; section increase is defined by minimumSectionIdentifierDelta. This allows sections as a list of individual separated from another pairs of start/end as well as a list of waypoints with the coordinate of an end of a section being the coordinate of the start of the next section, this array is always an array of latLng

  // latLng = [x,y];                                // x being -90 to 90, y being 0 to 360

  var currentSectionStartCoordinateIdentifier;      // index of coordinate in coordinates which is the start coordinate of the current section

  var firstSectionStartCoordinateIdentifier = 0;    // index of coordinate in coordinates which is the start coordinate of the first section, see also currentSectionNumber

  var minimumSectionStartCoordinateIdentifierDelta;

  var numberOfSections;

  var currentSectionNumber = 1;

  var R_earth = 6371;                               // radius of Earth in km

  var degToRad = Math.PI / 180;

  var ld = Math.log(2);

  var formerY = 0, formerZ = 0;
  /* end */


  /* constructor */
  function construct(wayData, dataOrdering) {
    coordinates = wayData;
    switch(dataOrdering) {
      case "waypoints":
        minimumSectionStartCoordinateIdentifierDelta = 1;
        numberOfSections = coordinates.length - 1;
        break;
      case "pairsOfSectionBounds":
        minimumSectionStartCoordinateIdentifierDelta = 2;
        numberOfSections = coordinates.length / 2 - 1;
        break;
      default:
    }
    currentSectionStartCoordinateIdentifier = firstSectionStartCoordinateIdentifier;
  }
  /* end */


  /* internal informative data api */
  function getNumberOfSectionIdentifiers() {
    return numberOfSections;
  }

  function getMinimumSectionStartCoordinateIdentifierDelta() {
    return minimumSectionStartCoordinateIdentifierDelta;
  }
  /* end */


  /* low-level data api */
  function getCurrentSectionStartCoordinateIdentifier() {
    return currentSectionStartCoordinateIdentifier;
  }

  function setCurrentSectionStartCoordinateIdentifier(toValue) {
    currentSectionStartCoordinateIdentifier = toValue;
  }

  function getSection(sectionStartCoordinateIdentifier) {
    var sectionStart = coordinates[sectionStartCoordinateIdentifier];
    var sectionEnd = coordinates[sectionStartCoordinateIdentifier + 1];
    return [sectionStart, sectionEnd];
  }

  // returns false when no next section
  function getNextSectionStartCoordinateIdentifier(doRestartWhenEnd) {
    var nextSectionStartCoordinateIdentifier = getCurrentSectionStartCoordinateIdentifier() + getMinimumSectionStartCoordinateIdentifierDelta();
    if(doRestartWhenEnd) {
      nextSectionStartCoordinateIdentifier %= getNumberOfSectionIdentifiers();
    }
    return (currentSectionNumber < numberOfSections) ? nextSectionStartCoordinateIdentifier : doRestartWhenEnd ? firstSectionStartCoordinateIdentifier : false;
  }
  /* end */


  /* high-level data api */
  function getCurrentSection() {
    return getSection(getCurrentSectionStartCoordinateIdentifier());
  }

  function advanceCurrentSection(doRestartWhenEnd) {
    var nextId = getNextSectionStartCoordinateIdentifier(doRestartWhenEnd);
    if(nextId !== false) {
      note.textContent = nextId;         // debug
      setCurrentSectionStartCoordinateIdentifier(nextId);
      currentSectionNumber++;
      if(doRestartWhenEnd) {
        currentSectionNumber %= numberOfSections;
      }
    }
  }

  // returns false when no next section
  function getNextSection(doRestartWhenEnd) {
    var nextId = getNextSectionStartCoordinateIdentifier(doRestartWhenEnd);
    return (nextId !== false) ? getSection(nextId) : nextId;
  }

  function isFirstSection() {
    return currentSectionStartCoordinateIdentifier === firstSectionStartCoordinateIdentifier;
  }
  /* end */


  /* logic api */
  // returns km
  function getLengthOfSection(section) {
    var lat1 = section[0][0] * degToRad;
    var lon1 = section[0][1] * degToRad;
    var lat2 = section[1][0] * degToRad;
    var lon2 = section[1][1] * degToRad;

    return R_earth * Math.acos(Math.sin(lat1) * Math.sin(lat2) + Math.cos(lat1) * Math.cos(lat2) * Math.cos(lon2 - lon1));
  }

  // returns in deg
  function getAngleBetweenSections(section1, section2) {
    // returns latLng
    function getPointOnSection(section, relativePoint) {
      var delta = getLengthOfSection(section) / R_earth;
      var sinDelta = Math.sin(delta);
      var relativeSinDeltaToEnd = Math.sin((1 - relativePoint) * delta) / sinDelta;
      var relativeSinDelta = Math.sin(relativePoint * delta) / sinDelta;
      var lat1 = section1[0][0] * degToRad;
      var lon1 = section1[0][1] * degToRad;
      var lat2 = section1[1][0] * degToRad;
      var lon2 = section1[1][1] * degToRad;
      var a = relativeSinDeltaToEnd * Math.cos(lat1);
      var b = relativeSinDelta * Math.cos(lat2);

      var x = a * Math.cos(lon1) + b * Math.cos(lon2);
      var y = a * Math.sin(lon1) + b * Math.sin(lon2);
      var z = relativeSinDeltaToEnd * Math.sin(lat1) + relativeSinDelta * Math.sin(lat2);

      return [Math.atan2(z, Math.sqrt(x * x + y * y)) / degToRad, Math.atan2(y, x) / degToRad];
    }

    // returns in rad
    function getBearing(point1, point2) {
      var lat1 = section1[0][0] * degToRad;
      var lon1 = section1[0][1] * degToRad;
      var lat2 = section1[1][0] * degToRad;
      var lon2 = section1[1][1] * degToRad;

      var cosLat2 = Math.cos(lat2);
      var deltaLon = lon2 - lon1;

      return Math.atan2(cosLat2 * Math.sin(deltaLon), Math.cos(lat1) * Math.sin(lat2) - Math.sin(lat1) * Math.cos(lat2) * Math.cos(deltaLon));
    }

    return 180 - Math.abs(getBearing(section2[0], getPointOnSection(section2, 0.01)) - getBearing(getPointOnSection(section1, 0.99), section1[1])) / degToRad;
  }

  // returns latLng
  function getPosition(aircraft) {
    return aircraft[1];
  }

  // returns in km/h
  function getSpeed(aircraft) {
    return aircraft[0];
  }

  function getZoomLevelFromFOV(km) {
    return Math.log(km / 13.28125) / ld;
  }
  /* end */


  /* auxiliary api */
  function clamp(value, min, max) {
    return Math.min(Math.max(value, min), max);
  }

  function getSmallerLength(viewport) {
    return viewport.clientWidth < viewport.clientHeight ? viewport.clientWidth : viewport.clientHeight;
  }
  /* end */


  // returns [zoomLevel, refreshDelay, nearingNextWayPoint]
  // auto-progresses internal waypoint
  this.getAutoValues = function(aircraft, viewport) {
    var v = getSpeed(aircraft);
    var nearingNextWayPoint = false;

    var position = getPosition(aircraft);
    var currentSection = getCurrentSection();
    var y = getLengthOfSection([position, currentSection[1]]);
    var nextSection = getNextSection();
    if(nextSection !== false) {
      var z = getLengthOfSection([position, nextSection[1]]);
      if(y - formerY > 0 && z - formerZ < 0 && (!isFirstSection() || v > 100)) {    // will advance unintended when take-off towards next section rather than current section
        advanceCurrentSection();
      }
      formerY = y;
      formerZ = z;
    }

    if(v > 100) {
      if(y > 13.28125 / 2 && nextSection !== false) {
        var b = 2 * (clamp(Math.cos(180 - getAngleBetweenSections(currentSection, nextSection)), 0, 1) * getLengthOfSection(nextSection) + y);
        var a = getZoomLevelFromFOV(b);
      } else {
        var a = 0;
        var b = 13.28125;
        nearingNextWayPoint = true;
      }
    } else {
      var a = 0;
      var b = 13.28125;
    }

    return [a, .1 + (3600 * b) / (getSmallerLength(viewport) * v), nearingNextWayPoint];
  };


  construct(wayData, dataOrdering);
}


var currentZoomLevel = 0;
var zoomingIn = false;
var lastZoomChangeTimeStamp = 0;
var data = [
  [-0.536351,-79.371948],[-2.127884,-79.594315],[-2.255389,-79.688828],[-2.158814,-79.886040],[-2.227518,-80.463356],[-2.207784,-80.985039],[-1.932615,-80.933044],[-1.867939,-80.825439],[-1.845166,-80.738426]
];
var x = new AutoFollow(data, "waypoints");

var pdi = document.createElement("iframe");
pdi.onload = function() {
  setTimeout(function() {
    exampleUse();
  }, 500);       // "wait" for _doc async gotten.
};
pdi.src = "progress.html";
pdi.style.pointerEvents = "none";
pdi.style.position = "absolute";
pdi.style.opacity = "0";
pdi.style.width = "1px";
pdi.style.height = "1px";
pdi.style.border = "0";
document.body.appendChild(pdi);

function exampleUse() {
  var pdicd = pdi.contentDocument;
  var speed = parseInt(pdicd.querySelector("table:nth-of-type(8) tr:nth-child(2) td:nth-child(2)").textContent.replace(".", ""), 10) * 1.852;
  var doc = pdicd.querySelector("#doc");
  while(doc.childElementCount > 0) {
      doc.removeChild(doc.firstElementChild);
  }
  var values = doc.textContent.split(" ");
  var latLng = [(values[3].charAt(0) === "S" ? -1 : 1) * (parseFloat(values[0]) + parseFloat(values[1]) / 60 + parseFloat(values[2].replace(",", ".")) / 3600), (values[7].charAt(0) === "W" ? -1 : 1) * (parseFloat(values[4]) + parseFloat(values[5]) / 60 + parseFloat(values[6].replace(",", ".")) / 3600)];
  function autoFollow() {
    var autoValues = x.getAutoValues([speed, latLng], document.querySelector("#mapcontainer"));
    if(autoValues[2]) {
      zoomIn();                     // knows target zoom = 0;
      zoomingIn = true;
    } else if(zoomingIn) {
      zoomOut(autoValues[0]);       // resets zoomingIn when autoValues[0] "reached"
    } else {
      currentZoomLevel = autoValues[0];
    }
    setZoom();
    setRefresh(autoValues[1]);
    function zoomIn() {
      if(lastZoomChangeTimeStamp === 0) {
        lastZoomChangeTimeStamp = performance.now();
      }
      var now = performance.now();
      var deltaTime = now - lastZoomChangeTimeStamp;
      lastZoomChangeTimeStamp = now;
      currentZoomLevel -= 3 * deltaTime / 1000;
      if(currentZoomLevel < 0) {
        currentZoomLevel = 0;
        lastZoomChangeTimeStamp = 0;
      }
    }
    function zoomOut(targetZoom) {
      if(lastZoomChangeTimeStamp === 0) {
        lastZoomChangeTimeStamp = performance.now();
      }
      var now = performance.now();
      var deltaTime = now - lastZoomChangeTimeStamp;
      lastZoomChangeTimeStamp = now;
      currentZoomLevel += 3 * deltaTime / 1000;
      if(currentZoomLevel > targetZoom) {
        currentZoomLevel = targetZoom;
        lastZoomChangeTimeStamp = 0;
        zoomingIn = false;
      }
    }
    function setZoom() {
      var slider = document.querySelector("#centerDistance");
      slider.value = currentZoomLevel;
      slider.dispatchEvent(new Event("input"));
    }
    function setRefresh(to) {
      var slider = document.querySelector("#refreshselect");
      slider.value = to;
      slider.dispatchEvent(new Event("change"));
      slider.dispatchEvent(new Event("input"));
    }
    setTimeout(function() {
      pdi.contentWindow.location.href = pdi.contentWindow.location.href;
    }, autoValues[1] * 1000);
  }
  autoFollow();
}