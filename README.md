Projet Système d'Exploitation 2019 : Robin Mathéus U21705221.

1.Fonctionnement :
  -Il y'a un mode insertion et normal. On peut lancer l'éditeur avec un fichier en argument ou non.
  Lorsque l'éditeur est lancé sans argument, l'utilisateur arrive sur une page d'accueil, il peut alors appuyer sur ENTER pour commencer à éditer ou bien entrer un nom de ficher que l'éditeur ouvrira ou créera si il n'existe pas.
  L'éditeur possède un mode insertion et normal, on peut supprimer avec BACKSPACE et DELETE, insérer une ligne avec ENTER, passer au mode insertion avec "i", et inversement avec "ESC", l'invité de commande complexe se déclenche avec ":" et s'arrête lorsque l'on efface toute la commande ou lorsque que l'on appuie sur ENTER. En mode normal, la touche "+" permet de se déplacer à la dernière ligne, "-" inversement. "/" déplace le curseur au milieu du fichier.
  L'éditeur possède un invité de commande, ":w fichier", ":w", ":q!", ":q", ":i". ":w" est pour la sauvegarde tandis que ":q" pour quitter. ":!q" permet d'esquiver la sauvegarde du fichier. ":i" nous renseigne diverses informations sur le fichier et l'utilisateur.  

2.Problèmes majeurs rencontrés lors de la conception : 
  - Choix du types de "buffer" pour le fichier : 
  Au départ, j'avais choisi un fichier .txt pour stocker les modifications sur le fichier en cours. Malheureusement je n'ai pas réussis à faire correspondre le curseur du terminal avec celui dans le fichier. J'ai par la suite fait un tableau de chaine dynamique. Ce tableau 
  stockait les lignes du fichier de longueur "nombre de colonne" mais il m'était difficile de retrouver le bon caractère par index par la suite.
  - Lecture de touches spéciales : 
  Pour la lecture de certaines touches, comme SUPPR, lorsque qu'on lit un caractère, il faut lire 2 octets supplémentaires pour faire une 
  "analyse" syntaxique de la séquence lue.
  - Implémentation de la souris. Le programme lit certains boutons de la souris et modifie la valeur de la variable globale correspondant au  curseur, mais le lecture d'une touche et de la souris ne se réalise pas en même temps. Il n'y a pas de curseur propre à la souris.

3.Limites de l'éditeur : 
  - Il n'y pas de Curseur pour la souris, et la lecture du fichier "dev/input/mice" et de STDIN_FILENO ne se fait pas en même temps.

4.Annexes :
* https://vt100.net/docs/vt100-ug/chapter3.html#ED 
	
