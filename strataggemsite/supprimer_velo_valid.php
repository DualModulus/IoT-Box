<!DOCTYPE html>
<meta charset="UTF-8"> 
<meta name="viewport" content="initial-scale=1.0, user-scalable=no" />
<html lang="fr">
<?php

	$nom = $_POST['nv_nom'];
	$type = $_POST['nv_type'];
	$devaddr = $_POST['devaddr'];
	$reset = $_POST['reset'];

	$base = new SQLite3('../bikes.db');
	if ($reset == 1)
		$base->exec('DELETE FROM designation WHERE devaddr = \''.$devaddr .'\';');

	echo '<script type="text/javascript">
					window.open("./index.php","_self");
			 </script>';

	$base->deconnect();
?>

</html>
