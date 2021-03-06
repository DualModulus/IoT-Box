<!DOCTYPE html>
<meta charset="UTF-8"> 
<meta name="viewport" content="initial-scale=1.0, user-scalable=no" />
<html lang="fr">
<head>

	<!--Base design Bootstrap-->
	<link rel="stylesheet" href="css/bootstrap.min.css">
	<link rel="stylesheet" href="css/bootstrap-toggle.min.css">
	<script src="js/bootstrap.min.js"></script>
	
	<!--Base design et animations MaterialDesign-->
	<link href="//cdn.muicss.com/mui-0.5.8/css/mui.min.css" rel="stylesheet" type="text/css" />
	<script src="//cdn.muicss.com/mui-0.5.8/js/mui.min.js"></script>
	<link rel="stylesheet" type="text/css" href="http://ajax.googleapis.com/ajax/libs/jqueryui/1.8/themes/base/jquery-ui.css" />

	<!--Design fait maison-->
	<link rel="stylesheet" href="css/strataggemsite.css">
	
	<!--Plugin GoogleMaps-->
	<script src="https://ajax.googleapis.com/ajax/libs/jquery/1.8.0/jquery.min.js"></script>
	<script src="https://ajax.googleapis.com/ajax/libs/jqueryui/1.8.23/jquery-ui.min.js"></script>
	<script src="http://maps.googleapis.com/maps/api/js"></script>
	<!--Icone GoogleMaps faite maison-->
	<script src="js/bikelocalization.js"></script>

  <!--Menu Latéral-->
	<script src="js/sidebar-menu.js"></script>

	<!--Onglets-->
	<script src="js/panes.js"></script>

	<?php
	$base = new SQLite3('../bikes.db');
	
	$query = "SELECT * FROM velos";
	$results = $base->query($query);
	$i=1;
	while ($row = $results->fetchArray())
	{	
		$id[$i] = $row['devaddr'];
		$latitude[$i] = $row['latitude'];
		$longitude[$i] = $row['longitude'];
	$i++;
	}
	$query = "SELECT * FROM designation";
	$results = $base->query($query);
	$i=1;
	while ($row = $results->fetchArray())
	{	
		$velo_id[$i] = $row['velo_id'];
		$velo_type[$i] = $row['velo_type'];
		$nom[$i] = $row['nom_proprio'];
	$i++;
	}
	?>
</head>

<body name="body">
	<center>
		<nav align="center" class="navbar navbar-default">
		  <div class="container-fluid">	
			  <ul class="nav navbar-nav navbar-left">	
				<a id="sidenav-toggle" class="sidenav-btn"><IMG SRC="img/straticon.png" align="middle" height="50px"></a>
			  </ul>		  
				<h1>StraTagGem Control Center&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</h1>
		  </div>	
		</nav>
		<nav id="sidenav" class="sidenav">
				<a class="mui-btn btn-block mui-btn--classic" href="./index.php">Accueil</a>   
			  <a class="mui-btn btn-block mui-btn--success" href="">Déconnection</a>    
				<a class="mui-btn btn-block mui-btn--warning" href="">Ajouter un Objet</a>
				<a class="mui-btn btn-block mui-btn--warning" href="./modifier_velo.php">Modifier un Objet</a>
				<a class="mui-btn btn-block mui-btn--dark" href="./ajouter_velo.php">Ajouter un Velo</a>
				<a class="mui-btn btn-block mui-btn--dark" href="./modifier_velo.php">Modifier un Velo</a>
		</nav>

		<div class="col-sm-4">
			<div class="panel panel-warning">
				<div class="panel-heading">
					<h2>Mes Clés</h2>
				</div>
				<div class="panel-body">
					<div>Retrouver l'objet portant le Tag1 ?
						<br><IMG SRC="img/keys.png" align="center" height="60px" title="opacity" class="rss opacity">
					</div>
					<form method="get" action="http://127.0.0.1:8080/tag1">
					<button id="boutton_light" type="submit" method="get" class="mui-btn mui-btn--warning mui-btn--raised">Retrouver</button>
					</form> 
				</div>
			</div>
		</div> 
		
  		<div class="col-sm-4">
			<div class="panel panel-warning">
				<div class="panel-heading">
					<h2>Mon Porte-Feuille</h2>
				</div>
				<div class="panel-body">
					<div>Retrouver l'objet portant le Tag2 ?
						<br><IMG SRC="img/wallet.png" align="center" height="60px" title="opacity" class="rss opacity">
					</div>
					<form method="get" action="http://127.0.0.1:8080/tag2">
					<button id="boutton_light" type="submit" method="get" class="mui-btn mui-btn--warning mui-btn--raised">Retrouver</button>
					</form> 
				</div>
			</div>
		</div> 

		<div class="col-sm-4">
			<div class="panel panel-warning">
				<div class="panel-heading">
					<button id="btn-modify-1" class="mui-btn round round-warning pull-right" type="button">+</button>
					<h2 class="pannel-with-right-btn">Ma Clé USB</h2>
				</div>
				<div class="panel-body">
					<div>Retrouver l'objet portant le Tag3 ?
						<br><IMG SRC="img/usb.png" align="center" height="60px" title="opacity" class="rss opacity">
					</div>
					<form method="get" action="http://127.0.0.1:8080/tag3">
					<button id="boutton_light" type="submit" method="get" class="mui-btn mui-btn--warning mui-btn--raised">Retrouver</button>
					</form>
				</div>
			</div>
		</div> 
		
		<div class="col-sm-12">
			<div class="panel panel-dark">
				<div class="panel-heading">
					<button onclick="location.href='./ajouter_velo.php';" class="mui-btn round round-dark pull-right">+</button>
					<h2 class="pannel-with-right-btn">Vélos</h2>
				</div>
				<div class="panel-body">
					<ul class="mui-tabs__bar mui-tabs__bar--justified">
					<?php
					for ($i=1; $i <= count($id); $i++)
					{	
						for ($j=1; $j <= count($id); $j++) {
							if ($id[$i] == $velo_id[$j])
							{ 
								echo '<li><a data-mui-toggle="tab" data-mui-controls="pane-events-'.$i.'" onclick="dropLocation({lat: '.$latitude[$i].', lng: '.$longitude[$i].'});">'.$velo_type[$j].' '.$nom[$j].'</a></li>';
							}
						}
					}
					?>
					</ul>
					<?php
					for ($i=1; $i <= count($id); $i++)
					{
						echo '<div class="mui-tabs__pane ';
						if ($i==1) echo'mui--is-active';
						echo '" id="pane-events-'.$i.'"></div>';
					}

					?>
					<div id="googleMap" style="width:100%;height:500px;border-bottom-left-radius:6px;border-bottom-right-radius:6px;"></div>
				</div>
			</div>
		</div> 
		<div class="col-sm-12" style="color: #f0e4d9;">&#9400; Thibaut GENTILI pour Strataggem (2016)</div>
	</center>

	<script type="text/javascript">
		$("#modify_thing").submit(function(e) {
			$("#body").attr("class","none");
			$("[id^='btn-modify']").attr("type","button");
			$(this).attr("value", "OK");
		});
	</script>
	
</body>
</html>
