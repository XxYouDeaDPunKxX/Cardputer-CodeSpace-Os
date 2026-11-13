#pragma once
#include "LocalControls.h"
#include "AppNavigationLabels.h"
constexpr int GUIDE_PAGES=LOCAL_GUIDE_PAGES;
inline const char* hudMainLabel(int i){return appToolLabel(i);}
inline const char* hudSystemLabel(int i){static const char* const a[]={"Verifica collegamenti","Modifica accesso","Diagnostica","Luce interna","Limite FPS","Qualita","Cursore","Monitor successivo","Aggiorna immagine","Disconnetti"};return i>=0&&i<10?a[i]:nullptr;}
inline const char* hudMenuLabel(int page,int row){return row>=0&&row<3?hudMainLabel(page+row):nullptr;}
struct GuideItem {const char* command;const char* text;};
struct GuideTopic {const char* title;GuideItem items[8];};
inline const GuideTopic* hudGuideTopic(int topic){
 static const GuideTopic topics[GUIDE_PAGES]={
  {"Leggere la guida", {{"Su / giu","Cambia argomento."},{"Invio / destra","Passa alla parte successiva dello stesso argomento."},{"Sinistra","Torna alla parte precedente."},{"Esc / Opt","Esc torna al menu. Opt chiude le impostazioni."},{"Due schermi","Prima di Linux leggi sul TFT; durante Linux leggi sull'HUD. Nei menu i tasti non scrivono a Linux."}}},
  {"Avvio e sessione", {{"Apri desktop","Wi-Fi noto e controlli automatici. Collega il desktop quando WSS/CXL e pronto."},{"Annulla","Durante il collegamento usa Esc, Opt o Annulla. Attendi il rilascio della connessione."},{"Avvia Codespace","Disponibile solo dopo che GitHub conferma la macchina ferma. Stato ignoto non significa macchina spenta."},{"R: verifica","Ricontrolla lo stato, senza avviare. Dopo 30 s di attesa incerta, appena termina la richiesta in corso, la verifica riparte automaticamente."},{"Scegli / attiva","I triangoli scelgono i pulsanti. Tab scorre; Invio attiva."}}},
  {"Reti Wi-Fi", {{"Reti Wi-Fi","Cerca una rete, sceglila e inserisci la password. R ripete la ricerca."},{"Profili salvati","Fino a otto reti, con recupero automatico. Una nuova rete si salva solo dopo connessione riuscita."},{"Nome manuale","Nello stesso elenco trovi Nome manuale e Dimentica rete."},{"Durante Linux","Le modifiche alla rete valgono al prossimo collegamento."}}},
  {"Accessi distinti", {{"Wi-Fi","Password della rete."},{"GitHub / Codespace","Nome della macchina e token GitHub."},{"Accesso desktop","Utente e password del ponte WSS; codice del desktop CXL."},{"Editor","Invio accetta. Opt annulla. Tab mostra o nasconde la password."},{"Conferma e salva","Controlla l'esito del salvataggio. La sessione aperta mantiene i propri accessi."}}},
  {"Tasti e modificatori", {{"Opt breve / tenuto","Tocco: Scrivi o Controlla. Tieni 1 s: impostazioni. Il cambio rilascia tasti e drag, conserva zoom e pan."},{"Scrivi / Fn","Scrivi invia tasti a Linux. Fn si accende e si spegne con un tocco."},{"Aa: tocco","Un tocco isolato sotto 350 ms blocca o sblocca Shift e i simboli blu stampati sulla tastiera."},{"Aa: tenuto","Vale Shift fino al rilascio. Usarlo con un altro input conserva il blocco precedente. In Controlla Aa cambia la precisione."},{"Ctrl / Alt","Tocco: prepara il prossimo tasto o tocco. Un secondo tocco annulla. Tenuti restano premuti."},{"Pagine operative","Seguono Fn, Ctrl, Alt, Super e Aa/Shift. Le pagine mostrano esempi: il risultato dipende dall'app attiva o dalla configurazione XFCE. CS OS invia i tasti."}}},
  {"Fn e Linux", {{"Fn + numeri","In Scrivi: 1..9 inviano F1..F9; 0 invia F10; - invia F11; = invia F12."},{"Fn + navigazione","Triangoli: direzioni. [ e ]: PagSu e PagGiu. H: Home. E: End. I: Insert."},{"Fn + altri tasti","P: Stampa. M: Menu. Del: Delete. Esc: Escape. Con Fn spento il tasto Esc scrive `."},{"Fn + Tab: Super","Un tocco prepara Super, il secondo annulla. Tenuto mantiene Super premuto."},{"Dopo Super","Fn resta acceso. Spegnilo per scrivere normalmente; Super preparato resta disponibile."}}},
  {"Puntatore", {{"Triangoli / Aa","I triangoli muovono il puntatore senza Fn. Aa cambia la precisione."},{"Invio: click","Al rilascio: sinistro. Tieni almeno 0,6 s: destro al rilascio."},{"Spazio: drag","Aggancia o rilascia il trascinamento. Esc lo rilascia senza cambiare tab."},{"Durante drag","Pennino e click con Invio sono sospesi."},{"Altri pulsanti","Apostrofo: centrale. Backslash: destro tenuto. [ e ]: rotella. Per digitare passa a Scrivi."}}},
  {"Vista", {{"E A S D: vista","In Controlla spostano solo l'inquadratura. I triangoli muovono il mouse."},{"Mouse fuori vista","La vista resta ferma fino al rientro del mouse; poi al bordo torna a scorrere."},{"Zoom / assetto","- e = regolano lo zoom. Tab alterna vista intera e nativa. [ e ]: rotella."},{"F: sposta finestra","I triangoli arancioni muovono la finestra. Invio fissa; Esc annulla."},{"Drag e pennino","Durante drag EASD non sposta la vista; i triangoli trascinano. Il pennino e attivo in entrambi gli assetti."}}},
  {"Pennino", {{"Tap / pressione","Tap: sinistro. Fermo almeno 0,6 s: destro nel punto toccato."},{"Posizione assoluta","Agisce nel punto toccato, non come un trackpad. Il pennino e consigliato per i bordi."},{"Trascinare","Passa a Controlla e usa Spazio."},{"Modificatori","Ctrl, Alt e Super preparati valgono per click, pennino e drag. Aa conserva il proprio stato per assetto."},{"Touch sospeso","Nessun touch remoto nei menu, durante drag o con TFT spento. Offline il pennino agisce sui controlli locali."}}},
  {"Opzioni e immagine", {{"Opt / menu","Tieni Opt per aprire, tocca per chiudere. Su/giu sceglie; Invio apre; Esc torna."},{"Durante Linux","Le opzioni restano nell'HUD; il desktop resta sul TFT."},{"Regola schermi","Luci, limite FPS, qualita, cursore, monitor e Aggiorna immagine."},{"SRV / MAX","SRV rispetta il limite del server. MAX rimuove il limite impostato dal Cardputer."},{"Codespace / audio","Codespace include Super singolo o per il prossimo tasto. Audio: ascolto e microfono separati, volume e boost."}}},
  {"Luci e memoria", {{"Regola una luce","Regola schermi: apri Luce interna o Luce esterna. Sinistra/destra cambia del 10%; Esc torna. Limiti: 10% e 100%."},{"Salvataggio","Luci, qualita e FPS si salvano automaticamente. In caso di errore il nuovo valore non e confermato per il riavvio."},{"TFT: spegni","Oscura il display senza chiudere Linux. Disponibile solo in sessione; il menu resta acceso."},{"Riaccendi il TFT","Tieni Opt, apri Regola schermi e TFT: accendi. Puoi anche cambiare Luce esterna. L'oscuramento e temporaneo."}}},
  {"Diagnosi e recupero", {{"Informazioni","Wi-Fi, GitHub e CXL. CPU e memoria sono del Cardputer, non del Codespace."},{"Log eventi","Guasti registrati, dal piu recente."},{"Verifica collegamenti","Ricontrolla lo stato senza avviare. WSS 404 non prova che il Codespace sia fermo."},{"Attesa avvio / arresto","Dopo 30 s di esito incerto, appena termina la richiesta in corso, riparte la verifica GitHub. R richiede un nuovo controllo; non reinvia avvio o arresto."},{"Limiti API / accesso","I limiti API restano rispettati. Per 401 o 403 verifica accesso e causa: il solo codice non prova l'origine del problema."},{"Sessione caduta","Torna al pre-desktop. Ricontrolla e premi Apri desktop quando e pronto."}}},
  {"Chiudere Linux", {{"Disconnetti","Chiude il desktop sul Cardputer e lascia Linux acceso."},{"Arresta Codespace","Richiede conferma e chiude macchina e processi. Spegnere il Cardputer non ferma subito il Codespace."},{"Presenza","Mantiene attivo l'host entro i limiti GitHub."},{"Stop / start","Conserva il disco, non finestre e processi. Rebuild o eliminazione possono perdere app e file."},{"Storage e quota","Lo storage e consumato anche da spento. Quota finita non significa cancellazione."}}},
 };
 return topic>=0&&topic<GUIDE_PAGES?&topics[topic]:nullptr;
}

inline const char* hudCommandLabel(LocalCommand command){
 switch(command){
 case LocalCommand::OpenAudio:return "Audio e microfono";
 case LocalCommand::OpenGuide:return "Guida ai comandi";case LocalCommand::OpenDiagnostics:return "Informazioni";case LocalCommand::OpenLogs:return "Log eventi";
 case LocalCommand::Retry:return "Verifica collegamenti";case LocalCommand::Configure:return "Accesso desktop";
 case LocalCommand::Wifi:return "Reti Wi-Fi";case LocalCommand::Management:return "GitHub / Codespace";
 case LocalCommand::Start:return "Avvia Codespace";case LocalCommand::Stop:return "Arresta Codespace";
 case LocalCommand::Disconnect:return "Disconnetti";
 case LocalCommand::SuperTap:return "Tasto Super";case LocalCommand::SuperNext:return "Prepara Super";
 case LocalCommand::Fps:return "Limite FPS";case LocalCommand::Quality:return "Qualita";
 case LocalCommand::Cursor:return "Cursore";case LocalCommand::Monitor:return "Monitor successivo";
 case LocalCommand::Refresh:return "Aggiorna immagine";case LocalCommand::Brightness:return "Luce interna";
 case LocalCommand::ExternalBrightness:return "Luce esterna";case LocalCommand::ExternalLight:return "TFT: spegni";
 case LocalCommand::Telemetry:return "Stato Cardputer";case LocalCommand::Info:return "Informazioni";
 default:return "Comando";}
}

// Action descriptions share the real command identifiers; no new commands.
inline const char* hudCommandHint(LocalCommand command){
 switch(command){
 case LocalCommand::OpenAudio:return "Audio e microfono";
 case LocalCommand::OpenGuide:return "Tastiera, pennino e comandi del desktop";
 case LocalCommand::OpenDiagnostics:return "Wi-Fi, collegamenti e misure del Cardputer";
 case LocalCommand::OpenLogs:return "Eventi registrati, dal piu recente";
 case LocalCommand::SuperTap:return "Invia il tasto Super a Linux";
 case LocalCommand::SuperNext:return "Super vale per il prossimo tasto o click";
 case LocalCommand::Retry:return "Ricontrolla rete, macchina e desktop";
 case LocalCommand::Configure:return "Credenziali del ponte WSS e codice CXL";
 case LocalCommand::Wifi:return "Scegli, salva o dimentica una rete";
 case LocalCommand::Management:return "Nome Codespace e token GitHub";
 case LocalCommand::Start:return "Avvia la macchina, poi verifica il desktop";
 case LocalCommand::Stop:return "Ferma macchina e processi";
 case LocalCommand::Disconnect:return "Chiude il desktop sul Cardputer";
 case LocalCommand::Brightness:case LocalCommand::ExternalBrightness:return "Apri la regolazione; sinistra e destra: 10%";
 case LocalCommand::ExternalLight:return "Solo in sessione; il menu resta acceso";
 case LocalCommand::Fps:return "Cambia limite; SRV usa quello del server";
 case LocalCommand::Quality:return "Regola la qualita dello streaming";
 case LocalCommand::Cursor:return "Mostra o nasconde il puntatore remoto";
 case LocalCommand::Monitor:return "Passa al monitor remoto successivo";
 case LocalCommand::Refresh:return "Richiede una nuova immagine completa";
 case LocalCommand::Telemetry:return "Misure del dispositivo e della rete";
 case LocalCommand::Info:return "Dispositivo e calibrazione del pennino";
 default:return "Invio: apri; Esc: indietro";
 }
}
