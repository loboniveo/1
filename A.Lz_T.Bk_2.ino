float i;
float valor;
bool listo = false;
float a;

void setup() {
  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);
  pinMode(16, OUTPUT);
  
  Serial.begin(115200);
  digitalWrite(5, HIGH);
  delay(100);
  digitalWrite(4, LOW);
  Serial.println("coloque un numero");
}

void loop() {
  // Leer valor desde el monitor serie
  if (Serial.available() > 0) {
    valor = Serial.parseInt();
    while (Serial.available() > 0) Serial.read();

    if (valor >= 0 && valor <= 3.3) {
      Serial.println("Apriete el boton para iniciar la medicion");
      i = analogReadMilliVolts(36) / 1000.0;

      Serial.println("ultimo valor");
      Serial.println(i);

      listo = true;
    } else {
      Serial.println("Valor invalido. Ingrese un numero entre 0 y 3.3.");
      listo = false;
    }
  }

  // Si se presiona el botón (pin 0 en LOW) y hay un valor listo
  if (digitalRead(0) == LOW && listo) {
    digitalWrite(5, LOW);
    delay(100);

    if (i > valor) {
      digitalWrite(16, LOW);
      delay(100);
      while (i >= valor) {
        digitalWrite(4, HIGH);
        delay(300);
        digitalWrite(4, LOW);
        delay(300);

        i  = analogReadMilliVolts(36) / 1000.0;
        Serial.println(i, 2);
      }
    }
    
    else if(i < valor) {
      digitalWrite(16, HIGH);
      delay(100);
      while (i <= valor) {
        digitalWrite(4, HIGH);
        delay(300);
        digitalWrite(4, LOW);
        delay(300);
        i = analogReadMilliVolts(36) / 1000.0;
        Serial.println(i);
      }
    }

    delay(100);
    digitalWrite(4, HIGH);
    delay(30);
    digitalWrite(5, HIGH);
    i = analogRead(36);

    a = analogReadMilliVolts(36) / 1000.0;
    Serial.print("valor final  ");
    Serial.print(a, 2);
    Serial.print(" V");
    
    listo = false;
  }
}