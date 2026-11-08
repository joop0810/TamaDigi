#include "i18n.h"
#include "pet.h"        // MED_COUNT
#include "move_names.h"
#include "zh_names.h"
#include "cjk_font.h"
#include <string.h>
#include <Preferences.h>

Lang gLang = LANG_DEFAULT;
static_assert(LANG_COUNT == MOVE_NAME_LANG_COUNT, "move-name language order must match Lang");

// Tabla de cadenas [idioma][id]. Sin acentos ni enes: la fuente bitmap del
// firmware no los tiene (por eso el espanol ya iba "Esta", "bano", etc.).
static const char *const STRINGS[LANG_COUNT][STR_COUNT] = {
  // ---------------- ES ----------------
  {
    "Esta digievolucionando!", "Nam nam!", "Le gusta!", "Tiene hambre!", "Necesita un bano!",
    "Esta agotado...", "Esta triste...", "Esta rellenito...", "Color raro!!", "Esta feliz",
    "GRACIAS! Hasta siempre", "Se ha escapado...", "Adios! Se despide...",
    "DIGIHUEVO", "Huevo legendario!?", "Huevo raro!", "Toca el huevo...", "Se mueve!", "Esta a punto!",
    "DIGIDEX %u/%u",
    "%s%s Nv.%u",
    "Soltar a %s?", "SI", "NO",
    "%u GOLPES", "FUERZA +%u", "NUEVO RECORD!", "RECORD: %u", "APORREA RAPIDO!",
    "PUNTOS: %u", "Que felicidad!", "+felicidad",
    "AJUSTAR HORA", "HORA", "MIN", "desliza arriba: cancelar", "Idioma",
    "MEDALLA!", "GENIAL!", "RACHA %u DIAS!",
    "RACHA %u  rec %u", "VIN", "FAVORITO ???", "CARNE", "PESCADO", "SETA",
    "%s   EDAD %lud", "toca el nombre: renombrar",
    "COMBATE", "FUE", "DEF", "VEL", "PES", "ENTRENAR FUERZA",
    "MEDALLAS %d/%d", "toca: volver",
    "NOMBRE:", "toca para volver",
    "COM", "FEL", "ENE", "LIM",
    "REC %u",
    "PROGRESO", "Nv.%u", "%u min para Nv.%u", "DIGIEVOLUCION", "Forma final",
    "Listo para digievolucionar!", "Victorias: %u/%u",
    "Digievoluciona en %u h", "Descuidos: %u",
    "SON ON", "SON OFF",
    "DIGIEVOLUCIONAR", "%s quiere decirte algo...", "%s se siente abandonado...",
    "Digievolucionar?", "Mantener forma", "Despedirse?", "Despedirse", "Quedaros juntos",
    "Elige tu digihuevo",
    "Sin sprites", "Cargalos en la SD",
    "PV", "IV %u",
    "MENU", "AJUSTES", "CERRAR", "EQUIPO %u/6", "EQUIPO", "- vacio -", "%s se une al equipo!", "Equipo lleno: elige a quien sustituir", "Dejarlo ir",
    "STATS", "ENTRENAR", "FUERZA", "VELOCIDAD", "DEFENSA", "Sube sola si esta a gusto",
    "MOVIMIENTOS", "- vacio -", "Elige movimiento", "Toca para cambiar", "POT %u", "ESTADO",
    "%s quiere aprender", "No aprender",
    "%s usa %s", "Es muy eficaz!", "No es muy eficaz...", "No le afecta...",
    "%s ha fallado!", "Golpe critico!", "%s se debilita!", "Se ha herido!",
    "%s: %s", "PARALISIS", "QUEMADURA", "VENENO", "DORMIDO", "CONGELADO", "CONFUSION",
    "Has ganado!", "Has perdido...",
    "%s saca a %s", "Adelante, %s!", "JEFES", "MEDALLAS %u/8", "DOMADOR", "VELOCIDAD +%u", "toca: cambiar avatar", "%u en total", "NORMAL", "DIFICIL",
 "ELEGIDOS %u/%u", "LUCHAR", "BLOQUEADO", "DIGIMON", "%s derrotado!", "MEDALLA NUEVA!", "VOL %u", "CAJA %u/%u", "cambiar con %s: elige hueco", "CAJA", "TRAER", "solo con un huevo", "COMBATE LAN", "CREAR", "UNIRSE", "buscando...", "listo!", "version distinta", "crear o unirse", "rival: %u mons", "el rival se fue", "esperando al rival...", "OTRA VEZ", "HUIR", "de que version es el huevo", "%s +%u", "ya no puede entrenar mas", "ELIGE TU VERSION", "RETIRAR", "Retirarla ya?", "la siguiente digievoluciona un dia mas tarde", "digievoluciona un dia mas tarde",   "SIN SPRITES", "SOLTAR", "se va para siempre", "AL EQUIPO", "no se unira a tu equipo",
    "MOCHILA", "MOCHILA VACIA", "EXPLORAR", "Aparece un %s salvaje!", "%s ha huido!",
    "Has escapado!", "No has podido escapar!", "Usas un %s", "%s escaneado!", "Escaneo fallido!",
    "Usas %s", "No ha hecho nada", "Encuentras %s!", "ACTIVAR", "CRIANDO", "LUZ %u", "REINICIAR", "Borrar todo?", "cada digimon, medalla y record se pierde", "TODOS", "CRIADOS", "EN MANO", "VARIANTE", "nada con este filtro", "HUECO", "NUMERO", "NIVEL", "AUDAZ", "FIRME", "VIVAZ", "RECIO", "ANIMOSO", "SERENO", "HOY", "%u/%u hoy", "cuidado hoy", "bien comido", "limpio", "contento", "descansado", "MEMORIA", "SECUENCIA %u", "MIRA", "TU TURNO", "ANIMO +%u", "A LA CAJA", "RECIBIENDO SPRITES", "%u archivos", "el juego vuelve al terminar", "Entreno: %u", "ATRIBUTO", "BEBE I", "BEBE II", "NOVATO", "CAMPEON", "ULTRA", "MEGA", "toca...",
  },
  // ---------------- EN ----------------
  {
    "Digivolving!", "Yum yum!", "It likes it!", "It's hungry!", "Needs a bath!",
    "Worn out...", "Feeling sad...", "A bit chubby...", "Rare colours!!", "It's happy",
    "THANKS! Farewell", "It ran away...", "Bye! Waving goodbye...",
    "DIGI-EGG", "Legendary egg!?", "Rare egg!", "Tap the egg...", "It moves!", "Almost there!",
    "DIGIDEX %u/%u",
    "%s%s Lv.%u",
    "Release %s?", "YES", "NO",
    "%u HITS", "STR +%u", "NEW RECORD!", "BEST: %u", "HIT FAST!",
    "SCORE: %u", "So much fun!", "+happiness",
    "SET TIME", "HOUR", "MIN", "swipe up: cancel", "Lang",
    "MEDAL!", "AWESOME!", "%u DAY STREAK!",
    "STREAK %u  best %u", "BOND", "FAVOURITE ???", "MEAT", "FISH", "MUSHROOM",
    "%s   AGE %lud", "tap name: rename",
    "BATTLE", "ATK", "DEF", "SPD", "WGT", "TRAIN STRENGTH",
    "MEDALS %d/%d", "tap: back",
    "NAME:", "tap to go back",
    "FOOD", "JOY", "ENE", "HYG",
    "BEST %u",
    "PROGRESS", "Lv.%u", "%u min to Lv.%u", "DIGIVOLUTION", "Final form",
    "Ready to digivolve!", "Wins: %u/%u",
    "Digivolves in %u h", "Care mistakes: %u",
    "SND ON", "SND OFF",
    "DIGIVOLVE!", "%s wants to tell you...", "%s feels abandoned...",
    "Digivolve?", "Keep form", "Say goodbye?", "Goodbye", "Stay together",
    "Choose your Digi-Egg",
    "No sprites", "Load them onto the SD",
    "HP", "IV %u",
    "MENU", "SETTINGS", "CLOSE", "PARTY %u/6", "PARTY", "- empty -", "%s joined the party!", "Party full: pick who to replace", "Let it go",
    "STATS", "TRAINING", "STRENGTH", "SPEED", "DEFENCE", "Grows on its own when happy",
    "MOVES", "- empty -", "Choose a move", "Tap a slot to change", "PWR %u", "STATUS",
    "%s wants to learn", "Do not learn",
    "%s used %s", "Super effective!", "Not very effective...", "It had no effect...",
    "%s missed!", "Critical hit!", "%s fainted!", "It hurt itself!",
    "%s: %s", "PARALYSED", "BURNED", "POISONED", "ASLEEP", "FROZEN", "CONFUSED",
    "You win!", "You lost...",
    "%s sends out %s", "Go, %s!", "BOSSES", "BADGES %u/8", "TAMER",
 "SPEED +%u", "tap: change avatar", "%u earned in all", "EASY", "HARD", "CHOSEN %u/%u", "FIGHT", "LOCKED", "DIGIMON", "%s defeated!", "NEW BADGE!", "VOL %u", "BOX %u/%u", "swap with %s: pick a slot", "BOX", "BRING BACK", "only while an egg waits", "LAN BATTLE", "HOST", "JOIN", "searching...", "ready!", "different version", "host or join", "rival: %u mons", "the rival left", "waiting for the rival...", "AGAIN", "RUN", "which version this egg is", "%s +%u", "trained as far as it can go", "CHOOSE A VERSION", "RETIRE", "Retire it early?", "the next one digivolves a day later", "digivolves a day later",
   "NO SPRITES", "RELEASE", "gone for good", "TO PARTY", "it will not join your party",
    "BAG", "BAG EMPTY", "EXPLORE", "A wild %s appeared!", "%s fled!",
    "You got away!", "You could not get away!", "You used a %s", "%s was scanned!", "The scan failed!",
    "Used %s", "It had no effect", "Found %s!", "MAKE ACTIVE", "RAISING", "LIGHT %u", "RESET GAME", "Erase everything?", "every Digimon, badge and record is gone", "ALL", "RAISED", "CAUGHT", "VARIANTE", "nothing matches this filter", "SLOT", "NUMBER", "LEVEL", "BOLD", "STURDY", "BRISK", "HARDY", "EAGER", "CALM", "TODAY", "%u/%u today", "cared for today", "well fed", "clean", "happy", "rested", "MEMORY", "SEQUENCE %u", "WATCH", "YOUR TURN", "JOY +%u", "TO BOX", "RECEIVING SPRITES", "%u files", "the game resumes when done", "Training: %u", "ATTRIBUTE", "BABY I", "BABY II", "ROOKIE", "CHAMPION", "ULTIMATE", "MEGA", "tap...",
  },
  // ---------------- FR ----------------
  {
    "Il digivolue!", "Miam miam!", "Il aime ca!", "Il a faim!", "Besoin d'un bain!",
    "Epuise...", "Triste...", "Un peu rond...", "Couleur rare!!", "Il est content",
    "MERCI! Adieu", "Il s'est enfui...", "Au revoir!",
    "DIGI-OEUF", "Oeuf legendaire!?", "Oeuf rare!", "Touche l'oeuf...", "Il bouge!", "Presque la!",
    "DIGIDEX %u/%u",
    "%s%s Niv.%u",
    "Relacher %s?", "OUI", "NON",
    "%u COUPS", "FORCE +%u", "NOUVEAU RECORD!", "RECORD: %u", "FRAPPE VITE!",
    "SCORE: %u", "Trop bien!", "+bonheur",
    "REGLER L'HEURE", "HEURE", "MIN", "glisse haut: annuler", "Langue",
    "MEDAILLE!", "SUPER!", "SERIE %u JOURS!",
    "SERIE %u  rec %u", "LIEN", "FAVORI ???", "VIANDE", "POISSON", "CHAMPIGNON",
    "%s   AGE %lud", "touche le nom: renommer",
    "COMBAT", "ATQ", "DEF", "VIT", "PDS", "ENTRAINER FORCE",
    "MEDAILLES %d/%d", "touche: retour",
    "NOM:", "touche pour revenir",
    "NOUR", "JOIE", "ENE", "HYG",
    "REC %u",
    "PROGRES", "Niv.%u", "%u min pour Niv.%u", "DIGIVOLUTION", "Forme finale",
    "Pret a digivoluer!", "Victoires: %u/%u",
    "Digivolue dans %u h", "Negligences: %u",
    "SON ON", "SON OFF",
    "DIGIVOLUER", "%s veut te parler...", "%s se sent abandonne...",
    "Digivoluer?", "Garder forme", "Dire adieu?", "Adieu", "Rester ensemble",
    "Choisis ton digi-oeuf",
    "Pas de sprites", "Charge-les sur la SD",
    "PV", "IV %u",
    "MENU", "REGLAGES", "FERMER", "EQUIPE %u/6", "EQUIPE", "- vide -", "%s rejoint l'equipe!", "Equipe pleine: qui remplacer?", "Le laisser partir",
    "STATS", "ENTRAINEMENT", "FORCE", "VITESSE", "DEFENSE", "Monte seule s'il est heureux",
    "CAPACITES", "- vide -", "Choisis une capacite", "Touche pour changer", "PUI %u", "STATUT",
    "%s veut apprendre", "Ne pas apprendre",
    "%s utilise %s", "Tres efficace!", "Peu efficace...", "Aucun effet...",
    "%s a rate!", "Coup critique!", "%s est K.O.!", "Il se blesse!",
    "%s: %s", "PARALYSIE", "BRULURE", "POISON", "ENDORMI", "GELE", "CONFUSION",
    "Gagne!", "Perdu...",
    "%s envoie %s", "Vas-y, %s!", "BOSS", "BADGES %u/8", "DOMPTEUR", "VITESSE +%u", "touche: changer d'avatar", "%u au total", "NORMAL", "DIFFICILE", "CHOISIS %u/%u", "COMBATTRE", "VERROUILLE", "DIGIMON", "%s vaincu!", "NOUVEAU BADGE!", "VOL %u", "BOITE %u/%u", "echanger avec %s: choisis", "BOITE", "RAMENER", "seulement avec un oeuf", "COMBAT LAN", "CREER", "REJOINDRE", "recherche...", "pret!", "version differente", "creer ou rejoindre", "rival: %u mons", "le rival est parti", "en attente du rival...", "ENCORE", "FUIR", "de quelle version est l oeuf", "%s +%u", "ne peut plus progresser", "CHOISIS TA VERSION", "RETIRER", "Retirer maintenant?", "le suivant digivolue un jour plus tard", "digivolue un jour plus tard",
   "SANS SPRITES", "RELACHER", "parti pour de bon", "A L EQUIPE", "ne rejoindra pas l equipe",
    "SAC", "SAC VIDE", "EXPLORER", "Un %s sauvage apparait!", "%s a fui!",
    "Tu as pris la fuite!", "Impossible de fuir!", "Tu utilises un %s", "%s est scanne!", "Scan rate!",
    "Tu utilises %s", "Aucun effet", "Tu trouves %s!", "RENDRE ACTIF", "EN ELEVAGE", "LUMIERE %u", "REINITIALISER", "Tout effacer?", "chaque digimon, badge et record est perdu", "TOUS", "ELEVES", "EN MAIN", "VARIANTE", "rien avec ce filtre", "CASE", "NUMERO", "NIVEAU", "AUDACE", "SOLIDE", "VIF", "ROBUSTE", "ARDENT", "CALME", "AUJOURD HUI", "%u/%u aujourd hui", "soigne aujourd hui", "bien nourri", "propre", "content", "repose", "MEMOIRE", "SEQUENCE %u", "REGARDE", "A TOI", "JOIE +%u", "A LA BOITE", "RECEPTION DES SPRITES", "%u fichiers", "le jeu reprend a la fin", "Entrainement: %u", "ATTRIBUT", "BEBE I", "BEBE II", "DISCIPLE", "CHAMPION", "ULTIME", "MEGA", "touche...",
  },
  // ---------------- DE ----------------
  {
    "Digitiert!", "Mampf mampf!", "Gefaellt ihm!", "Hat Hunger!", "Braucht ein Bad!",
    "Erschoepft...", "Traurig...", "Etwas rundlich...", "Seltene Farbe!!", "Es ist froh",
    "DANKE! Lebwohl", "Es ist weg...", "Tschuess! Winkt",
    "DIGI-EI", "Legendaeres Ei!?", "Seltenes Ei!", "Beruehre das Ei...", "Es bewegt sich!", "Fast soweit!",
    "DIGIDEX %u/%u",
    "%s%s Lv.%u",
    "%s freilassen?", "JA", "NEIN",
    "%u TREFFER", "KRAFT +%u", "NEUER REKORD!", "REKORD: %u", "SCHNELL HAUEN!",
    "PUNKTE: %u", "Wie schoen!", "+Freude",
    "ZEIT STELLEN", "STD", "MIN", "hoch wischen: abbruch", "Sprache",
    "MEDAILLE!", "TOLL!", "%u TAGE SERIE!",
    "SERIE %u  rek %u", "BND", "LIEBLING ???", "FLEISCH", "FISCH", "PILZ",
    "%s   ALTER %lud", "Name tippen: umbenennen",
    "KAMPF", "ANG", "VER", "INI", "GEW", "KRAFT TRAINIEREN",
    "MEDAILLEN %d/%d", "tippen: zurueck",
    "NAME:", "tippen zum zurueck",
    "ESS", "FRO", "ENE", "HYG",
    "REK %u",
    "FORTSCHRITT", "Lv.%u", "%u min bis Lv.%u", "DIGITATION", "Endform",
    "Bereit zur Digitation!", "Siege: %u/%u",
    "Digitiert in %u Std.", "Pflegefehler: %u",
    "TON AN", "TON AUS",
    "DIGITIEREN", "%s will dir etwas sagen...", "%s fuehlt sich verlassen...",
    "Digitieren?", "Form behalten", "Abschied?", "Lebwohl", "Zusammen bleiben",
    "Waehle dein Digi-Ei",
    "Keine Sprites", "Auf die SD laden",
    "LP", "IV %u",
    "MENU", "EINSTELLUNGEN", "SCHLIESSEN", "TEAM %u/6", "TEAM", "- leer -", "%s kommt ins Team!", "Team voll: wen ersetzen?", "Ziehen lassen",
    "WERTE", "TRAINING", "STAERKE", "TEMPO", "ABWEHR", "Steigt von selbst bei guter Laune",
    "ATTACKEN", "- leer -", "Attacke waehlen", "Tippen zum Aendern", "STK %u", "STATUS",
    "%s will lernen", "Nicht lernen",
    "%s setzt %s ein", "Sehr effektiv!", "Nicht sehr effektiv...", "Keine Wirkung...",
    "%s hat verfehlt!", "Volltreffer!", "%s wurde besiegt!", "Es verletzt sich!",
    "%s: %s", "PARALYSE", "VERBRANNT", "VERGIFTET", "SCHLAEFT", "GEFROREN", "VERWIRRT",
    "Gewonnen!", "Verloren...",
    "%s schickt %s", "Los, %s!", "BOSSE", "ORDEN %u/8", "TAMER", "TEMPO +%u", "tippen: Avatar wechseln", "%u insgesamt", "NORMAL", "SCHWER", "GEWAEHLT %u/%u", "KAEMPFEN", "GESPERRT", "DIGIMON", "%s besiegt!", "NEUER ORDEN!", "LAUT %u", "BOX %u/%u", "mit %s tauschen: waehle", "BOX", "ZURUECK", "nur mit einem Ei", "LAN KAMPF", "HOSTEN", "BEITRETEN", "suche...", "bereit!", "andere Version", "hosten oder beitreten", "Gegner: %u", "der Gegner ist weg", "warte auf den Gegner...", "NOCHMAL", "FLUCHT", "aus welcher Version das Ei ist", "%s +%u", "kann nicht weiter trainieren", "WAEHLE DEINE VERSION", "VERABSCHIEDEN", "Jetzt verabschieden?", "das naechste digitiert einen Tag spaeter", "digitiert einen Tag spaeter",
   "SPRITES FEHLEN", "FREILASSEN", "fuer immer weg", "INS TEAM", "kommt nicht ins team",
    "BEUTEL", "BEUTEL LEER", "ERKUNDEN", "Ein wildes %s erscheint!", "%s ist geflohen!",
    "Du bist entkommen!", "Flucht gescheitert!", "Du nutzt einen %s", "%s gescannt!", "Scan fehlgeschlagen!",
    "%s benutzt", "Keine Wirkung", "%s gefunden!", "AKTIV MACHEN", "WIRD AUFGEZOGEN", "LICHT %u", "ZURUCKSETZEN", "Alles loschen?", "jedes Digimon, Orden und Rekord ist weg", "ALLE", "AUFGEZOGEN", "DABEI", "VARIANTE", "nichts mit diesem Filter", "PLATZ", "NUMMER", "LEVEL", "KUEHN", "ROBUST", "FLINK", "ZAEH", "EIFRIG", "RUHIG", "HEUTE", "%u/%u heute", "heute gepflegt", "gut genaehrt", "sauber", "gluecklich", "ausgeruht", "MERKEN", "FOLGE %u", "SCHAU", "DU BIST DRAN", "FREUDE +%u", "IN DIE BOX", "EMPFANGE SPRITES", "%u Dateien", "danach geht es weiter", "Training: %u", "ATTRIBUT", "BABY I", "BABY II", "ROOKIE", "CHAMPION", "ULTRA", "MEGA", "tippen...",
  },
  // ---------------- IT ----------------
  {
    "Digievolve!", "Gnam gnam!", "Gli piace!", "Ha fame!", "Vuole un bagno!",
    "Esausto...", "Triste...", "Un po' cicciotto...", "Colore raro!!", "E' felice",
    "GRAZIE! Addio", "E' scappato...", "Ciao! Saluta",
    "DIGI-UOVO", "Uovo leggendario!?", "Uovo raro!", "Tocca l'uovo...", "Si muove!", "Ci siamo quasi!",
    "DIGIDEX %u/%u",
    "%s%s Lv.%u",
    "Liberare %s?", "SI", "NO",
    "%u COLPI", "FORZA +%u", "NUOVO RECORD!", "RECORD: %u", "COLPISCI VELOCE!",
    "PUNTI: %u", "Che gioia!", "+felicita",
    "IMPOSTA ORA", "ORA", "MIN", "scorri su: annulla", "Lingua",
    "MEDAGLIA!", "GRANDE!", "SERIE %u GIORNI!",
    "SERIE %u  rec %u", "LEG", "PREFERITO ???", "CARNE", "PESCE", "FUNGO",
    "%s   ETA %lud", "tocca il nome: rinomina",
    "LOTTA", "ATT", "DIF", "VEL", "PES", "ALLENA FORZA",
    "MEDAGLIE %d/%d", "tocca: indietro",
    "NOME:", "tocca per tornare",
    "CIB", "GIO", "ENE", "IGI",
    "REC %u",
    "PROGRESSI", "Lv.%u", "%u min per Lv.%u", "DIGIEVOLUZIONE", "Forma finale",
    "Pronto a digievolvere!", "Vittorie: %u/%u",
    "Digievolve tra %u h", "Disattenzioni: %u",
    "AUD ON", "AUD OFF",
    "DIGIEVOLVI", "%s vuole dirti qualcosa...", "%s si sente abbandonato...",
    "Digievolvere?", "Mantieni forma", "Salutare?", "Addio", "Restare insieme",
    "Scegli il digi-uovo",
    "Senza sprite", "Caricali sulla SD",
    "PS", "IV %u",
    "MENU", "IMPOSTAZIONI", "CHIUDI", "SQUADRA %u/6", "SQUADRA", "- vuoto -", "%s entra in squadra!", "Squadra piena: chi sostituire?", "Lasciarlo andare",
    "STATS", "ALLENAMENTO", "FORZA", "VELOCITA", "DIFESA", "Sale da sola se sta bene",
    "MOSSE", "- vuoto -", "Scegli una mossa", "Tocca per cambiare", "POT %u", "STATO",
    "%s vuole imparare", "Non imparare",
    "%s usa %s", "Superefficace!", "Non molto efficace...", "Nessun effetto...",
    "%s ha mancato!", "Brutto colpo!", "%s e\' esausto!", "Si e ferito!",
    "%s: %s", "PARALISI", "SCOTTATURA", "VELENO", "ADDORMENTATO", "CONGELATO", "CONFUSIONE",
    "Hai vinto!", "Hai perso...",
    "%s manda %s", "Vai, %s!", "BOSS", "MEDAGLIE %u/8", "DOMATORE", "VELOCITA +%u", "tocca: cambia avatar", "%u in totale", "NORMALE", "DIFFICILE", "SCELTI %u/%u", "LOTTA", "BLOCCATO", "DIGIMON", "%s sconfitto!", "NUOVA MEDAGLIA!", "VOL %u", "BOX %u/%u", "scambia con %s: scegli", "BOX", "RIPORTA", "solo con un uovo", "LOTTA LAN", "CREA", "ENTRA", "ricerca...", "pronto!", "versione diversa", "crea o entra", "rivale: %u mons", "il rivale se n' e andato", "in attesa del rivale...", "ANCORA", "FUGGI", "da quale versione viene l uovo", "%s +%u", "non puo allenarsi oltre", "SCEGLI LA VERSIONE", "RITIRARE", "Ritirarla adesso?", "il prossimo digievolve un giorno dopo", "digievolve un giorno dopo",
   "SENZA SPRITE", "LIBERA", "via per sempre", "AL GRUPPO", "non entrera nel gruppo",
    "ZAINO", "ZAINO VUOTO", "ESPLORA", "Appare un %s selvatico!", "%s e fuggito!",
    "Sei riuscito a fuggire!", "Non sei riuscito a fuggire!", "Usi un %s", "%s scansionato!", "Scansione fallita!",
    "Usi %s", "Nessun effetto", "Trovi %s!", "RENDI ATTIVO", "IN ALLEVAMENTO", "LUCE %u", "AZZERA", "Cancellare tutto?", "ogni digimon, medaglia e record va perso", "TUTTI", "ALLEVATI", "CON TE", "VARIANTE", "nulla con questo filtro", "POSTO", "NUMERO", "LIVELLO", "AUDACE", "SOLIDO", "LESTO", "TENACE", "ARDITO", "CALMO", "OGGI", "%u/%u oggi", "curato oggi", "ben nutrito", "pulito", "contento", "riposato", "MEMORIA", "SEQUENZA %u", "GUARDA", "TOCCA A TE", "GIOIA +%u", "NEL BOX", "RICEZIONE SPRITE", "%u file", "il gioco riprende alla fine", "Allenamento: %u", "ATTRIBUTO", "BEBE I", "BEBE II", "PRINCIPIANTE", "CAMPIONE", "EVOLUTO", "MEGA", "tocca...",
  },
  // ---------------- PT ----------------
  {
    "Digievoluindo!", "Nham nham!", "Ele gosta!", "Esta com fome!", "Precisa de banho!",
    "Exausto...", "Triste...", "Um pouco gordinho...", "Cor rara!!", "Esta feliz",
    "OBRIGADO! Adeus", "Fugiu...", "Tchau! Acena",
    "DIGI-OVO", "Ovo lendario!?", "Ovo raro!", "Toque no ovo...", "Mexe-se!", "Quase la!",
    "DIGIDEX %u/%u",
    "%s%s Niv.%u",
    "Soltar %s?", "SIM", "NAO",
    "%u GOLPES", "FORCA +%u", "NOVO RECORDE!", "RECORDE: %u", "BATA RAPIDO!",
    "PONTOS: %u", "Que alegria!", "+alegria",
    "AJUSTAR HORA", "HORA", "MIN", "deslize cima: cancelar", "Idioma",
    "MEDALHA!", "OTIMO!", "%u DIAS SEGUIDOS!",
    "SEQ %u  rec %u", "LACO", "FAVORITO ???", "CARNE", "PEIXE", "COGUMELO",
    "%s   IDADE %lud", "toque no nome: renomear",
    "COMBATE", "ATQ", "DEF", "VEL", "PES", "TREINAR FORCA",
    "MEDALHAS %d/%d", "toque: voltar",
    "NOME:", "toque para voltar",
    "COM", "ALE", "ENE", "HIG",
    "REC %u",
    "PROGRESSO", "Niv.%u", "%u min para Niv.%u", "DIGIEVOLUCAO", "Forma final",
    "Pronto a digievoluir!", "Vitorias: %u/%u",
    "Digievolui em %u h", "Descuidos: %u",
    "SOM ON", "SOM OFF",
    "DIGIEVOLUIR", "%s quer dizer-te algo...", "%s sente-se abandonado...",
    "Digievoluir?", "Manter forma", "Despedir?", "Adeus", "Ficar juntos",
    "Escolhe o digi-ovo",
    "Sem sprites", "Carrega-os no SD",
    "PS", "IV %u",
    "MENU", "AJUSTES", "FECHAR", "EQUIPA %u/6", "EQUIPA", "- vazio -", "%s junta-se a equipa!", "Equipa cheia: quem substituir?", "Deixa-lo ir",
    "STATS", "TREINO", "FORCA", "VELOCIDADE", "DEFESA", "Sobe sozinha se estiver bem",
    "GOLPES", "- vazio -", "Escolhe um golpe", "Toca para mudar", "POT %u", "ESTADO",
    "%s quer aprender", "Nao aprender",
    "%s usa %s", "Super eficaz!", "Pouco eficaz...", "Nao teve efeito...",
    "%s falhou!", "Acerto critico!", "%s desmaiou!", "Feriu-se!",
    "%s: %s", "PARALISIA", "QUEIMADURA", "VENENO", "A DORMIR", "CONGELADO", "CONFUSAO",
    "Ganhaste!", "Perdeste...",
    "%s envia %s", "Vai, %s!", "CHEFES", "MEDALHAS %u/8", "DOMADOR", "VELOCIDADE +%u", "toca: mudar avatar", "%u no total", "NORMAL", "DIFICIL", "ESCOLHIDOS %u/%u", "LUTAR", "BLOQUEADO", "DIGIMON", "%s derrotado!", "NOVA MEDALHA!", "VOL %u", "CAIXA %u/%u", "trocar com %s: escolhe", "CAIXA", "TRAZER", "so com um ovo", "COMBATE LAN", "CRIAR", "ENTRAR", "a procurar...", "pronto!", "versao diferente", "criar ou entrar", "rival: %u mons", "o rival saiu", "a esperar pelo rival...", "OUTRA VEZ", "FUGIR", "de que versao e o ovo", "%s +%u", "ja nao pode treinar mais", "ESCOLHE A VERSAO", "REFORMAR", "Reformar agora?", "o proximo digievolui um dia depois", "digievolui um dia depois",
   "SEM SPRITES", "SOLTAR", "vai para sempre", "A EQUIPA", "nao entrara na equipa",
    "MOCHILA", "MOCHILA VAZIA", "EXPLORAR", "Um %s selvagem aparece!", "%s fugiu!",
    "Conseguiste fugir!", "Nao conseguiste fugir!", "Usas um %s", "%s digitalizado!", "Falhou o scan!",
    "Usas %s", "Sem efeito", "Encontras %s!", "TORNAR ATIVO", "A CRIAR", "LUZ %u", "REINICIAR", "Apagar tudo?", "cada digimon, medalha e record perde-se", "TODOS", "CRIADOS", "NA MAO", "VARIANTE", "nada com este filtro", "LUGAR", "NUMERO", "NIVEL", "AUDAZ", "FIRME", "AGIL", "RIJO", "ANIMADO", "CALMO", "HOJE", "%u/%u hoje", "cuidado hoje", "bem alimentado", "limpo", "contente", "descansado", "MEMORIA", "SEQUENCIA %u", "OLHA", "E A TUA VEZ", "ANIMO +%u", "PARA A CAIXA", "A RECEBER SPRITES", "%u ficheiros", "o jogo volta no fim", "Treino: %u", "ATRIBUTO", "BEBE I", "BEBE II", "NOVATO", "CAMPEAO", "ULTIMATE", "MEGA", "toque...",
  },
  // ---------------- ZH (simplified Chinese; drawn with the CJK pixel font) ----------------
  {
    "进化中!", "好吃好吃!", "它很喜欢!", "肚子饿了!", "需要洗澡!", "累坏了...", "有点难过...", "有点胖了...", "稀有颜色!!", "很开心",
    "谢谢! 再见", "它逃走了...", "再见! 挥手告别...", "数码蛋", "传说之蛋!?", "稀有蛋!", "摸摸蛋...", "它动了!", "快孵化了!",
    "数码图鉴 %u/%u", "%s%s Lv.%u", "放生%s?", "是", "否", "%u 连击", "力量 +%u", "新纪录!", "最佳: %u", "快点击!",
    "得分: %u", "太好玩了!", "+快乐", "设置时间", "时", "分", "上滑: 取消", "语言", "奖章!", "太棒了!", "连续 %u 天!",
    "连续 %u  最佳 %u", "羁绊", "最爱 ???", "肉", "鱼", "蘑菇", "%s   年龄 %lu天", "点名字: 改名", "战斗", "攻击", "防御",
    "速度", "体重", "力量训练", "奖章 %d/%d", "点击: 返回", "名字:", "点击返回", "饱食", "快乐", "体力", "清洁", "最佳 %u", "进度",
    "Lv.%u", "%u 分钟到 Lv.%u", "进化", "最终形态", "可以进化了!", "胜场: %u/%u", "%u 小时后进化", "照顾失误: %u", "音效 开",
    "音效 关", "进化!", "%s 有话想说...", "%s 觉得被抛弃了...", "要进化吗?", "保持形态", "要告别吗?", "再见", "继续在一起", "选择你的数码蛋",
    "没有图像", "请把图像存入SD卡", "HP", "IV %u", "菜单", "设置", "关闭", "队伍 %u/6", "队伍", "- 空 -", "%s 加入了队伍!",
    "队伍已满: 选择替换谁", "放走它", "能力", "训练", "力量", "速度", "防御", "开心时会自己成长", "招式", "- 空 -", "选择招式",
    "点击栏位来更换", "威力 %u", "变化", "%s 想学习", "不学习", "%s 使用了 %s", "效果拔群!", "效果不好...", "没有效果...",
    "%s 没打中!", "会心一击!", "%s 倒下了!", "攻击了自己!", "%s: %s", "麻痹", "烧伤", "中毒", "睡眠", "冰冻", "混乱", "你赢了!",
    "你输了...", "%s 派出了 %s", "上吧, %s!", "首领", "徽章 %u/8", "驯兽师", "速度 +%u", "点击: 换头像", "共获得 %u 个", "简单",
    "困难", "已选 %u/%u", "战斗", "未解锁", "数码兽", "打败了 %s!", "新徽章!", "音量 %u", "盒子 %u/%u", "与 %s 交换: 选一格",
    "盒子", "带回来", "只在有蛋等待时", "局域网对战", "建立", "加入", "搜索中...", "准备好了!", "版本不同", "建立或加入", "对手: %u 只",
    "对手离开了", "等待对手...", "再来", "逃跑", "这个蛋属于哪个版本", "%s +%u", "已经训练到极限了", "选择版本", "退休", "提前让它退休?",
    "下一只会晚一天进化", "晚一天进化", "没有图像", "放生", "永远离开", "放入队伍", "它不会加入你的队伍", "背包", "背包是空的", "探索",
    "野生的 %s 出现了!", "%s 逃走了!", "成功逃脱!", "逃不掉!", "你使用了 %s", "扫描了 %s!", "扫描失败!", "使用了 %s", "没有效果",
    "找到了 %s!", "设为当前", "培育中", "亮度 %u", "重置游戏", "全部删除?", "所有数码兽, 徽章和纪录都会消失", "全部", "培育过", "捕获", "异色",
    "没有符合的结果", "栏位", "编号", "等级", "大胆", "坚韧", "敏捷", "强壮", "热情", "冷静", "今天", "今天 %u/%u", "今天照顾过了",
    "吃饱了", "干净", "开心", "休息好了", "记忆", "序列 %u", "看好", "轮到你了", "快乐 +%u", "放入盒子", "正在接收图像", "%u 个文件",
    "完成后游戏继续", "训练: %u", "属性", "幼年期I", "幼年期II", "成长期", "成熟期", "完全体", "究极体", "点击...",
  },
};

// Nombres de medalla en sus tres longitudes [idioma][medalla].
static const char *const MED_NAME[LANG_COUNT][MED_COUNT] = {
  { "Nv.10", "Nv.25", "Nv.50", "COMIDA", "RACHA 7", "VINCULO", "FORMA TOPE", "EN FORMA" },
  { "Lv.10", "Lv.25", "Lv.50", "FOOD", "7 STREAK", "BOND", "TOP FORM", "IN SHAPE" },
  { "Niv.10", "Niv.25", "Niv.50", "REPAS", "SERIE 7", "LIEN", "FORME MAX", "EN FORME" },
  { "Lv.10", "Lv.25", "Lv.50", "ESSEN", "7 SERIE", "BINDUNG", "ENDFORM", "FIT" },
  { "Lv.10", "Lv.25", "Lv.50", "CIBO", "SERIE 7", "LEGAME", "FORMA MAX", "IN FORMA" },
  { "Niv.10", "Niv.25", "Niv.50", "COMIDA", "SEQ 7", "LACO", "FORMA MAX", "EM FORMA" },
  { "Lv.10", "Lv.25", "Lv.50", "美食", "连续7天", "羁绊", "巅峰", "健康" },
};
static const char *const MED_LBL[LANG_COUNT][MED_COUNT] = {
  { "Nv10", "Nv25", "Nv50", "COMI", "7DIAS", "VINC", "TOPE", "SANO" },
  { "Lv10", "Lv25", "Lv50", "FOOD", "7DAYS", "BOND", "TOP", "FIT" },
  { "Niv10", "Niv25", "Niv50", "REPAS", "7JRS", "LIEN", "MAX", "FORME" },
  { "Lv10", "Lv25", "Lv50", "ESSEN", "7TAGE", "BND", "END", "FIT" },
  { "Lv10", "Lv25", "Lv50", "CIBO", "7GG", "LEG", "MAX", "FIT" },
  { "Niv10", "Niv25", "Niv50", "COMI", "7DIAS", "LACO", "MAX", "FIT" },
  { "Lv10", "Lv25", "Lv50", "美食", "7天", "羁绊", "巅峰", "健康" },
};
static const char *const MED_DSC[LANG_COUNT][MED_COUNT] = {
  { "NIVEL 10", "NIVEL 25", "NIVEL 50", "COMIDA FAVORITA",
    "RACHA 7 DIAS", "VINCULO MAX", "FORMA FINAL", "EN FORMA" },
  { "LEVEL 10", "LEVEL 25", "LEVEL 50", "FAVOURITE FOOD",
    "7 DAY STREAK", "MAX BOND", "FINAL FORM", "IN SHAPE" },
  { "NIVEAU 10", "NIVEAU 25", "NIVEAU 50", "PLAT PREFERE",
    "SERIE 7 JOURS", "LIEN MAX", "FORME FINALE", "EN FORME" },
  { "LEVEL 10", "LEVEL 25", "LEVEL 50", "LIEBLINGSESSEN",
    "7 TAGE SERIE", "MAX BINDUNG", "ENDFORM", "FIT" },
  { "LIVELLO 10", "LIVELLO 25", "LIVELLO 50", "CIBO PREFERITO",
    "SERIE 7 GIORNI", "LEGAME MAX", "FORMA FINALE", "IN FORMA" },
  { "NIVEL 10", "NIVEL 25", "NIVEL 50", "COMIDA FAVORITA",
    "SEQ 7 DIAS", "LACO MAX", "FORMA FINAL", "EM FORMA" },
  { "等级 10", "等级 25", "等级 50", "最爱的食物", "连续 7 天", "羁绊满点", "最终形态", "身体健康" },
};

const char *moveName(uint8_t move) {
  if (move >= MOVE_COUNT) move = MV_NONE;
  return MOVE_NAME_TBL[gLang][move];
}

const char *T(StrId id) { return STRINGS[gLang][id]; }

const char *L(const char *en) {
  if (gLang != LANG_ZH || !en) return en;
  int lo = 0, hi = ZH_NAME_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = strcmp(en, ZH_NAMES[mid].en);
    if (!c) return ZH_NAMES[mid].zh;
    if (c < 0) hi = mid - 1; else lo = mid + 1;
  }
  return en;
}

int tcols(const char *s) {
  int n = 0;
  if (!s) return 0;
  for (const uint8_t *p = (const uint8_t *)s; *p; p++) {
    if (*p < 0x80) n++;               // ASCII
    else if ((*p & 0xE0) == 0xE0) n += 2;  // lead byte of a 3/4-byte sequence: CJK
    else if ((*p & 0xE0) == 0xC0) n += 1;  // 2-byte sequence (no glyph; one column)
    // continuation bytes (10xxxxxx) add nothing
  }
  return n;
}

const uint8_t *cjkGlyph(uint32_t cp) {
  int lo = 0, hi = CJK_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (CJK_CODES[mid] == cp) return CJK_BITS[mid];
    if (CJK_CODES[mid] < cp) lo = mid + 1; else hi = mid - 1;
  }
  return nullptr;
}
const char *medalName(int i)  { return MED_NAME[gLang][i]; }
const char *medalLabel(int i) { return MED_LBL[gLang][i]; }
const char *medalDesc(int i)  { return MED_DSC[gLang][i]; }

void loadLang() {
  Preferences p;
  p.begin("tamadigi", true);  // solo lectura
  uint8_t v = p.getUChar("lang", LANG_DEFAULT);
  p.end();
  gLang = (v < LANG_COUNT) ? (Lang)v : LANG_DEFAULT;
}

void setLang(Lang l) {
  if (l >= LANG_COUNT) return;
  gLang = l;
  Preferences p;
  p.begin("tamadigi", false);
  p.putUChar("lang", (uint8_t)l);
  p.end();
}
