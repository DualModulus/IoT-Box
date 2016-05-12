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
	
	
  <!--Menu Latéral-->
	<script src="js/sidebar-menu.js"></script>

	<script>
		function verifierFormulaire()
		{
			var form = document.getElementById("formulaire");
			if(form.devaddr.value == '')
			{
				alert("Veuillez choisir un ensemble à réassocier !");
				return false;
			}
			if(form.nv_nom.value == '')
			{
				alert("Veuillez entrer un nouveau nom de propriétaire !");
				return false;
			}
			if(form.nv_type.value == '')
			{
				alert("Veuillez choisir un nouveau type de vélo!");
				return false;
			}
			return true;
		}
		
		function verifierDevaddr()
		{
		var form = document.getElementById("formulaire");
			if(form.devaddr.value == '')
			{
				alert("Veuillez choisir un ensemble à dissocier !");
				return false;
			}
		return true;
		}
	</script>
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

		<div class="col-sm-12">
			<div class="panel panel-dark">
				<div class="panel-heading">
					<h2>Modifier un Vélo</h2>
				</div>
				<div class="panel-body">
					<div class="col-lg-6 col-lg-offset-3">

					<?php
					//Connection BDD
					$base = new SQLite3('../bikes.db');
	
					//Requêtes sur les 2 tables
					$query = "SELECT devaddr FROM velos;";
					$results = $base->query($query);
					$i=1;
					while ($row = $results->fetchArray())
					{	
						$id[$i] = $row['devaddr'];
						$i++;
					}
					$query = "SELECT * from designation;";
					$results = $base->query($query);
					$i=1;
					while ($row = $results->fetchArray())
					{	
						$velo_id[$i] = $row['velo_id'];
						$velo_type[$i] = $row['velo_type'];
						$nom[$i] = $row['nom_proprio'];
					$i++;
					}

					//Champs vides
					$nv_nom = "";
					$nv_type = "";
					$devaddr = "";

					//Debut du formulaire :
					echo '<form id="formulaire" name="formulaire" action="modifier_velo_valid.php" method="POST"><p>
								<br>';
								//Menu déroulant pour le choix de la société :
								echo '<div class="form-group">
											<label for="select" class="control-label">Association Lampe Velo Propriétaire</label>
											<br>
											<select multiple="" class="form-control" name="devaddr">';

								//Possibilité d'association à un numéro de lampe que si celui-ci n'est pas déja utilisé par une autre association
								for ($i=1; $i <= count($id); $i++)
								{	
									for ($j=1; $j <= count($id); $j++) {
										if ($id[$i] == $velo_id[$j])
										{ 
											echo '<option id="'.$id[$i].'" value="'.$id[$i].'" '.$selected.'>'.$id[$i].' : '.$velo_type[$j].' '.$nom[$j].'</option>';
										}
									}
								}

								echo '</select>
								</div><br>
								<div class="form-group">
									<label for="inputUser" class="control-label">Nouveau Nom de propriétaire</label>
									<div class="input-group col-sm-6">
										<input id="site" type="text" class="form-control panel-default" name="nv_nom" value="'.$nv_nom.'" placeholder="Exemple : Mr Yguel">
									</div>
								</div>
								<br>
								<div class="form-group">
										<label for="inputType" class="control-label">Nouveau Type de vélo</label>
										<br>
										<select multiple="" class="form-control" name="nv_type" value="'.$nv_type.'">
										<option value="Classique">Vélo classique</option>
										<option value="Electrique">Vélo électrique</option>
										<option value="VTT">VTT</option>
										<option value="VTC">VTC</option>
										<option value="Fixy">Fixy</option>
										<option value="BMX">BMX</option>
										<option value="Tandem">Tandem</option>
										</select>
								</div>


								<div class="form-group">
									<br><br>
									<button type="submit" class="mui-btn mui-btn--dark" onclick="return verifierFormulaire();">Valider</button>
									<br>
									<button type="submit" class="mui-btn mui-btn--alert" onclick="return verifierDevaddr();" name="reset" value="1">Dissocier</button>
									<br>
									<button type="back" class="mui-btn mui-btn--classic" name="BouttonRetour"  Onclick="javascript:window.open(\'index.php\',\'_self\')">RETOUR</button>
								</div>
								<br>
								</div>
							</form>
						</div>
					</div>
				</div>
			</div>
			<div class="col-sm-12" style="color: #f0e4d9;">&#9400; Thibaut GENTILI pour Strataggem (2016)</div>
		</center>
	</body>';

	$base->deconnect();
	?>
	
</html>


