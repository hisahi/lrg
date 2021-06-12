.TH LRG 1
.SH NIMI
lrg \- näyttää rivejä tietyillä rivinumeroilla tiedostoista
.SH YLEISKATSAUS
.B lrg
[\fI\,VALITSIN\/\fR]... \fI\,ALUE\fR[,\fI\,ALUE\/\fR]... [\fI\,TIEDOSTO\/\fR]...
.SH KUVAUS
.B lrg
näyttää rivejä, joilla on tietyt rivinumerot, tiedostoista.

TIEDOSTOja voi määritellä yhden tai useamman. Jos yhtään tiedostoa ei
määritellä,
.B lrg
lukee rivit standardisyötteestä. Tiedosto - luetaan myös standardisyötteestä.
Jos tiedostoja määritellään useampi kuin yksi, jokainen niistä käsitellään
yksi kerrallaan peräjälkeen; kaikki annetut alueet luetaan tiedostosta ennen
seuraavaan tiedostoon siirtymistä.

ALUE määrittelee näytettävät rivit. Alueelle on kolme eri sallittua muotoa.
Seuraavissa muodoissa N ja M vastaavat positiivisia kokonaislukuja, ellei toisin
mainita:

.RS
.IP \fBN\fP
Yksi rivinumero; näyttää rivin N. Rivit numeroidaan yhdestä alkaen, eli
ensimmäisen rivin rivinumero on 1.
.IP \fBN\-[M]\fP
Rivinumeroväli. Numeroväliin sisältyy molemmat ääriarvot (sekä ensimmäinen että
viimeinen rivi). Jos N on suurempi kuin M, yhtään riviä ei näytetä. Jos M
jätetään pois, kaikki tiedoston rivit näytetään rivistä N alkaen.
.IP \fBN~[M]\fP
Rivinumero ja sen lähiympäristö. M määrittää, kuinka monta riviä näytetään ennen
riviä N ja rivin N jälkeen. Jos M jätetään pois, sen oletusarvo on 3. Tämä alue
näyttää korkeintaan 2M+1 riviä. M voi myös olla 0, jolloin vain rivi N
näytetään.
.RE

Useamman kuin yhden alueen voi antaa erottamalla ne pilkuilla.

Jos
.B lrg
yrittää näyttää rivin, jota tiedostossa ei ole (kuten riviä 100 tiedostosta,
jossa on vain 50 riviä), rivialueen lukeminen loppuu, mutta jos niitä on vielä
jäljellä, käsittely jatkuu niistä nykyisen tiedoston kohdalla.

.SH VALITSIMET
.TP
\fB\-e\fR, \fB\-\-error\-on\-eof\fR
pidä tiedoston loppumista virheenä. tiedoston lukeminen päättyy, jos riviä ei
pystytä lukemaan, ja ohjelma palauttaa virhekoodin 1 jos yksikään tiedosto
loppui liian aikaisin. ei tulosta virheviestiä automaattisesti; katso
valitsin \fB\-w\fR
.TP
\fB\-f\fR, \fB\-\-file\-names\fR
näytä tiedostonimet ennen jokaista tiedostoa
.TP
\fB\-l\fR, \fB\-\-line\-numbers\fR
näytä rivinumerot ennen jokaista riviä
.TP
\fB\-w\fR, \fB\-\-warn\-eof\fR
näytä varoitus, jos tiedosto loppuu ennen kuin rivialueen riviä voidaan lukea
.TP
\fB\-\-lps=\fI\,NUM\/\fR, \fB\-\-lines\-per\-second=\fI\,NUM\/\fR
näytä rivit tietyllä nopeudella. NUM määrittää nopeuden riveinä sekunnissa,
ja se voi olla myös desimaaliluku. sen on oltava 0.001:n (1/1000) ja 1000000:n
(yhden miljoonan) väliltä. LPS 1 näyttää noin yhden rivin sekunnissa, eli
jokaisen rivin välillä on yhden sekunnin viive. LPS on saatavilla vain, jos se
on käännetty ohjelmaan
.TP
\fB\-?\fR, \fB\-\-help\fR
näytä ohje ja lopeta suoritus
.TP
\fB\-\-version\fR
näytä versiotiedot ja lopeta suoritus
.SH LOPETUSKOODI
Nämä koodit pätevät POSIX-yhteensopivissa ympäristöissä. Komento palauttaa
koodin 0, jos se suoritettiin onnistuneesti, koodin 1 jos tapahtui virhe, ja
koodin 2 jos komentoa yritettiin käyttää väärin. Tiedoston loppumista ennen kuin
tiettyä riviä voitiin lukea ei pidetä virheenä, ellei valitsinta \fB\-e\fR,
\fB\-\-error\-on\-eof\fR käytetä.
.SH HUOMIOT
Jos luettava tiedosto ei tue kelaamista (kuten esimerkiksi jos sitä luetaan
putkesta standardisyötteen kautta), alue voi yrittää näyttää vain rivejä, jotka
sijaitsevat tiedostossa myöhemmin kuin mikään siihen asti näytetty rivi; jos
alue yrittää toistaa aiemmin näytetyn rivin tai näyttää rivin, joka edeltää
jotain jo näytetyistä riveistä, ohjelman suoritus päättyy virheeseen.
.SH ESIMERKKI
.TP
lrg -f 10 *.c
Tulosta rivi 10 jokaisesta C-lähdekooditiedostosta nykyisestä hakemistosta.
Tiedostonimi tulostetaan ennen jokaista riviä.
.TP
lrg -l 1-10
Tulosta kymmenen ensimmäistä riviä standardisyötteestä ja rivinumerot ennen
jokaista riviä.
.TP
lrg 105~5,250 /usr/share/dict/words
Jos sanakirja on asennettu, tulosta rivit 100-110 (5 riviä rivin 105 ympäriltä)
sekä rivin 250 sanaluettelosta.
.SH TEKIJÄT
Ohjelman ja ohjeen kirjoittanut Sampo Hippeläinen (hisahi).
.SH VIKAILMOITUKSET
\fB\,lrg\fR\-ohjelmaa ylläpidetään osoitteessa
https://github.com/hisahi/lrg

Vetopyynnöt ja ongelmailmoitukset otetaan vastaan.
.SH KATSO MYÖS
.BR head(1),
.BR tail(1)
.PP