#include <Arduino.h>
#include <Servo.h>
#include <Stepper.h>

// Pinos
const int PINO_MOTOR_IN1 = 10;
const int PINO_MOTOR_IN2 = 11;
const int PINO_MOTOR_IN3 = 12;
const int PINO_MOTOR_IN4 = 13;
const int PASSOS_POR_REVOLUCAO = 2048;

const int PINO_SERVO_PORTA = 7;
const int PINO_ULTRA_TRIG = 6;
const int PINO_ULTRA_ECHO = 5;
const int PINO_BOTOES_EXTERNOS = A0;

// Andares (4 Andares: 0, 1, 2, 3)
const int PASSOS_ANDAR[] = { 0, 6500, 11000, 16500 };
const int TAMANHO_ANDAR_ARRAY = sizeof(PASSOS_ANDAR) / sizeof(PASSOS_ANDAR[0]);
const int NUMERO_ANDAR_MAXIMO = TAMANHO_ANDAR_ARRAY - 1;

// Configurações
const int INTERVALO_MOVIMENTO_PASSO = 1;  // ms/passo
const int ANGULO_PORTA_FECHADA = 180;
const int ANGULO_PORTA_ABERTA = 0;
const int DISTANCIA_PORTA_BLOQUEADA = 50;  // cm
const long TEMPO_PORTA_ABERTA = 3000;       // 3 segundos

// Constantes
const int SUBINDO = 1;
const int DESCENDO = -1;
const int PARADO = 0;
const int TOLERANCIA_LEITURA = 15;  // Tolerância da leitura dos botões

// Objetos
Stepper motorElevador(PASSOS_POR_REVOLUCAO, PINO_MOTOR_IN1, PINO_MOTOR_IN3, PINO_MOTOR_IN2, PINO_MOTOR_IN4);
Servo motorPorta;

// Variáveis de Estado
enum EstadoElevador { EST_PARADO, EST_MOVENDO, EST_PORTA_ABERTA };
EstadoElevador estadoAtual = EST_PARADO;

long passosAtuaisElevador = PASSOS_ANDAR[0];
int andarAtual = 0;
int direcaoAtual = PARADO;

// FILAS DE PEDIDOS
bool pedidosInternos[TAMANHO_ANDAR_ARRAY] = { false };
bool pedidosExternosSubir[TAMANHO_ANDAR_ARRAY] = { false };
bool pedidosExternosDescer[TAMANHO_ANDAR_ARRAY] = { false };

// Variáveis de Tempo e Leitura
unsigned long tempoUltimoMovimento = 0;
unsigned long tempoAberturaPorta = 0;
int ultimoValorBotaoLido = 0;

// Protótipos
bool temPedidosAcima(int andar);
bool temPedidosAbaixo(int andar);

void setup() {
  Serial.begin(9600);

  motorElevador.setSpeed(10);
  motorPorta.attach(PINO_SERVO_PORTA);

  pinMode(PINO_ULTRA_TRIG, OUTPUT);
  pinMode(PINO_ULTRA_ECHO, INPUT);
  pinMode(PINO_BOTOES_EXTERNOS, INPUT);

  motorPorta.write(ANGULO_PORTA_FECHADA);
  delay(500);

  Serial.println("Elevador Iniciado (Modo Inteligente)");
  Serial.println("Andar Atual: 0 | Estado: PARADO");
}

void loop() {
  lerPedidosInternosSerial();
  lerPedidosExternos();
  gerenciarMaquinaEstados();
}

void lerPedidosInternosSerial() {
  if (Serial.available() > 0) {
    char input = Serial.read();
    if ((input >= '0') && (input <= ('0' + NUMERO_ANDAR_MAXIMO))) {
      int andarPedido = input - '0';
      if (!pedidosInternos[andarPedido]) {
        pedidosInternos[andarPedido] = true;
        Serial.print("Pedido Interno (Serial): Andar ");
        Serial.println(andarPedido);
      }
    }
  }
}

void lerPedidosExternos() {
  int valorLido = analogRead(PINO_BOTOES_EXTERNOS);
  
  if (valorLido < 300) {
    ultimoValorBotaoLido = 0;
    return;
  }

  if (abs(valorLido - ultimoValorBotaoLido) < TOLERANCIA_LEITURA) {
    return;
  }
  Serial.print("Valor lido: ");
  Serial.println(valorLido);

  if (valorLido > 860 && valorLido < 1000) { // Térreo sobe
    if (!pedidosExternosSubir[0]) {
      pedidosExternosSubir[0] = true;
      Serial.println("Pedido: Andar 0 (Subir)");
    }
  }
  else if (valorLido > 750 && valorLido < 860) { // 1 Sobe
    if (!pedidosExternosSubir[1]) {
      pedidosExternosSubir[1] = true;
      Serial.println("Pedido: Andar 1 (Subir)");
    }
  }
  else if (valorLido > 685 && valorLido < 750) { // 1 Desce
    if (!pedidosExternosDescer[1]) {
      pedidosExternosDescer[1] = true;
      Serial.println("Pedido: Andar 1 (Descer)");
    }
  }
  else if (valorLido > 640 && valorLido < 685) { // 2 Sobe
    if (!pedidosExternosSubir[2]) {
      pedidosExternosSubir[2] = true;
      Serial.println("Pedido: Andar 2 (Subir)");
    }
  }
  else if (valorLido > 580 && valorLido < 630) { // 2 Desce
    if (!pedidosExternosDescer[2]) {
      pedidosExternosDescer[2] = true;
      Serial.println("Pedido: Andar 2 (Descer)");
    }
  }
  else if (valorLido > 300 && valorLido < 550) { // 3 Desce
    if (!pedidosExternosDescer[3]) {
      pedidosExternosDescer[3] = true;
      Serial.println("Pedido: Andar 3 (Descer)");
    }
  }

  ultimoValorBotaoLido = valorLido;
}

void gerenciarMaquinaEstados() {
  unsigned long tempoAtual = millis();
  long passosAlvo;
  int andarQuePassamos;

  switch (estadoAtual) {
    case EST_PARADO:
      // 1. Verifica se deve abrir a porta no andar atual
      if (devePararNoAndarAtual(andarAtual, direcaoAtual)) {
        iniciarAberturaPorta();
        break;
      }

      // 2. Decide a próxima direção
      if (direcaoAtual == PARADO) {
        if (temPedidosAcima(andarAtual)) {
          direcaoAtual = SUBINDO;
          estadoAtual = EST_MOVENDO;
        } else if (temPedidosAbaixo(andarAtual)) {
          direcaoAtual = DESCENDO;
          estadoAtual = EST_MOVENDO;
        }
      } 
      else if (direcaoAtual == SUBINDO) {
        if (temPedidosAcima(andarAtual)) {
          estadoAtual = EST_MOVENDO;
        } else if (temPedidosAbaixo(andarAtual)) {
          direcaoAtual = DESCENDO;
          estadoAtual = EST_MOVENDO;
        } else {
          direcaoAtual = PARADO;
        }
      } 
      else if (direcaoAtual == DESCENDO) {
        if (temPedidosAbaixo(andarAtual)) {
          estadoAtual = EST_MOVENDO;
        } else if (temPedidosAcima(andarAtual)) {
          direcaoAtual = SUBINDO;
          estadoAtual = EST_MOVENDO;
        } else {
          direcaoAtual = PARADO;
        }
      }
      break;

    case EST_MOVENDO:
      if ((tempoAtual - tempoUltimoMovimento) < INTERVALO_MOVIMENTO_PASSO) {
        return;
      }
      tempoUltimoMovimento = tempoAtual;

      passosAlvo = (direcaoAtual == SUBINDO) ? PASSOS_ANDAR[NUMERO_ANDAR_MAXIMO] : PASSOS_ANDAR[0];

      if (passosAtuaisElevador != passosAlvo) {
        int passoParaMover = (direcaoAtual == SUBINDO) ? 1 : -1;
        motorElevador.step(passoParaMover);
        passosAtuaisElevador += passoParaMover;

        andarQuePassamos = passosCorrespondeAndar(passosAtuaisElevador);
        if (andarQuePassamos != -1) {
          andarAtual = andarQuePassamos;

          if (devePararNoAndarAtual(andarAtual, direcaoAtual)) {
            Serial.print("Parando no andar ");
            Serial.println(andarAtual);
            iniciarAberturaPorta();
          }
        }
      } else {
        Serial.print("Extremo atingido: ");
        Serial.println(andarAtual);
        estadoAtual = EST_PARADO;
      }
      break;

    case EST_PORTA_ABERTA:
      if (tempoAtual - tempoAberturaPorta < TEMPO_PORTA_ABERTA) {
        return;
      }
      if (portaObstruida()) {
        Serial.println("!! OBSTRUÇÃO !!");
        tempoAberturaPorta = tempoAtual;
      } else {
        iniciarFechamentoPorta();
      }
      break;
  }
}

bool devePararNoAndarAtual(int andar, int direcao) {
  // 1. Pedido Interno
  if (pedidosInternos[andar]) {
    return true;
  }

  // 2. Se está PARADO, atende qualquer chamado neste andar (AQUI ESTÁ A CORREÇÃO DO SEU PEDIDO)
  if (direcao == PARADO) {
    if (pedidosExternosSubir[andar] || pedidosExternosDescer[andar]) {
      return true;
    }
  }

  // 3. Lógica de Movimento (Mesma direção)
  if (direcao == SUBINDO && pedidosExternosSubir[andar]) {
    return true;
  }
  if (direcao == DESCENDO && pedidosExternosDescer[andar]) {
    return true;
  }

  // 4. Inversão de Sentido Inteligente
  if (direcao == SUBINDO && pedidosExternosDescer[andar]) {
    if (!temPedidosAcima(andar)) {
      return true;
    }
  }
  if (direcao == DESCENDO && pedidosExternosSubir[andar]) {
    if (!temPedidosAbaixo(andar)) {
      return true;
    }
  }

  return false;
}

bool temPedidosAcima(int andar) {
  for (int i = andar + 1; i <= NUMERO_ANDAR_MAXIMO; i++) {
    if (pedidosInternos[i] || pedidosExternosSubir[i] || pedidosExternosDescer[i]) {
      return true;
    }
  }
  return false;
}

bool temPedidosAbaixo(int andar) {
  for (int i = andar - 1; i >= 0; i--) {
    if (pedidosInternos[i] || pedidosExternosSubir[i] || pedidosExternosDescer[i]) {
      return true;
    }
  }
  return false;
}

int passosCorrespondeAndar(long passos) {
  for (int i = 0; i <= NUMERO_ANDAR_MAXIMO; i++) {
    if (PASSOS_ANDAR[i] == passos) {
      return i;
    }
  }
  return -1;
}

void iniciarAberturaPorta() {
  Serial.print("Abrindo porta (Andar ");
  Serial.print(andarAtual);
  Serial.println(")");
  
  motorPorta.write(ANGULO_PORTA_ABERTA);

  // Limpa pedido interno
  pedidosInternos[andarAtual] = false;

  // --- LIMPEZA DE PEDIDOS EXTERNOS ATUALIZADA ---
  
  if (direcaoAtual == PARADO) {
    // Se estava parado, limpa ambos os pedidos desse andar para não travar em loop
    pedidosExternosSubir[andarAtual] = false;
    pedidosExternosDescer[andarAtual] = false;
  } 
  else {
    // Se estava em movimento, usa a lógica inteligente de direção
    if (direcaoAtual == SUBINDO) {
      pedidosExternosSubir[andarAtual] = false;
      if (!temPedidosAcima(andarAtual)) {
         pedidosExternosDescer[andarAtual] = false;
      }
    }

    if (direcaoAtual == DESCENDO) {
      pedidosExternosDescer[andarAtual] = false;
      if (!temPedidosAbaixo(andarAtual)) {
         pedidosExternosSubir[andarAtual] = false;
      }
    }
  }

  tempoAberturaPorta = millis();
  estadoAtual = EST_PORTA_ABERTA;
}

void iniciarFechamentoPorta() {
  Serial.println("Fechando porta...");
  motorPorta.write(ANGULO_PORTA_FECHADA);
  estadoAtual = EST_PARADO;
  delay(1000);
}

bool portaObstruida() {
  long duracao;
  int distancia;
  digitalWrite(PINO_ULTRA_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PINO_ULTRA_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PINO_ULTRA_TRIG, LOW);
  duracao = pulseIn(PINO_ULTRA_ECHO, HIGH);
  distancia = duracao * 0.034 / 2;
  return (distancia < DISTANCIA_PORTA_BLOQUEADA && distancia > 0);
}
