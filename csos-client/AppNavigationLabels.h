#pragma once
inline const char* appToolLabel(int index){
 static const char* const labels[]={"Regola schermi","Reti e accesso","Guida e diagnostica","Codespace"};
 return index>=0&&index<4?labels[index]:nullptr;
}

inline const char* appToolHint(int index){
 static const char* const hints[]={"Luminosita e qualita video","Wi-Fi e credenziali","Guida, misure ed eventi","Avvio e arresto della macchina"};
 return index>=0&&index<4?hints[index]:"Seleziona una voce";
}
