#include <HX711.h>
#include <WiFi.h>
#include <WebServer.h>

const int DOUT = 21;
const int CLOCK = 22;
const int LED  = 2;
const char* SSID = "RCP_entrenador";
const char* PASSWORD = "rcp12345";
float valorActual= 0.0;

HX711 scale ;
WebServer server (80);

  const char paginaHTML[]= R"rawliteral(
   <!DOCTYPE html>
   <html lang = "es">
   <head>
    <meta charset ="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title> Entrenador en RCP</title>
  </head>
  <body>
   <h1>Entrenador en RCP </h1>
   <div id ="valor">o.o kg</div>

<script>
function actualizar (){
  fetch("/datos")
  .then(res => res.json())
  .then(data=>{
    document.getElementById("valor").innerHTML=data.valor.toFixed(1) + "kg";
  })
  .catch(err =>{})
  .finally(() => {
    setTimeout(actualizar,300);

  });
}
   actualizar();
  </script>
  
 </body>
 </html>
 )rawliteral";

void handleRoot(){
  server.send(200,"text/html",paginaHTML);
}

void handleDatos(){ 
  if (scale.is_ready()){
    valorActual = scale.get_units(1);
    if (valorActual < 0){
      valorActual =0.0;
    }
  }


char json[40];
snprintf(json,sizeof(json),"{\"valor\":%.1f}", valorActual);
server.send(200,"text/plain",json);
}




void setup (){
  Serial.begin(115200);
  scale.begin(DOUT,CLOCK);
  scale.set_scale(43200);
  scale.tare();
  pinMode(LED,OUTPUT);
  digitalWrite(LED,LOW);

  

  WiFi.mode(WIFI_AP);
  bool comprobacion = WiFi.softAP(SSID,PASSWORD);
  if (comprobacion == pdTRUE){
    digitalWrite(LED,HIGH);
  }
  else{
    Serial.println("error al iniciar el AP");
    digitalWrite(LED,LOW);
  }
 
  Serial.println(WiFi.softAPIP());
  server.on("/", handleRoot);
  server.on("/datos",handleDatos);
  server.begin();



}
void loop(){
 
  
  delay (200);
  server.handleClient();
}