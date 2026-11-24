# Birds&Cashews: plano de desenvolvimento

Projeto de Linguagem de Programação I, semestre 2026.2, prof. Derzu Omaia. **Entrega: 07/12/2026.**

Este documento define o jogo: regras, mecânicas, arquitetura, requisitos do enunciado e cronograma. Para compilar e
jogar, veja o [`README.md`](../README.md).

## 1. Resumo do jogo

Você é um pássaro em primeira pessoa, controlado pelo próprio corpo diante da webcam, em uma ilha de cajueiros gigantes.
Você coleta cajus, leva ao seu ninho e decide entre comer (melhora o pássaro) ou investir (melhora o ninho). Até 6
jogadores dividem o mundo pela rede local, conversam por voz de proximidade e podem roubar cajus uns dos outros.

## 2. Decisões fechadas

| Tema | Decisão |
|---|---|
| Plataforma | App desktop Linux em C++17, com OpenGL, SDL2 e OpenCV DNN |
| Rede | Um jogador hospeda, os outros encontram a sala na LAN. De 1 a 6 jogadores (host e até 5 clientes) |
| Estrutura | Mundo aberto contínuo, sem fim de partida |
| Interação | Roubo de cajus de ninhos rivais e colisão em mergulho derruba a carga |
| Coleta | Automática por proximidade, com o bico abrindo sozinho |
| Bico | Também abre enquanto o jogador fala |
| Ninho | Menu por postura, segurando para confirmar |
| NPCs | Ecossistema: pequenos em bando, médios competem por cajus, grandes caçam |
| Salvamento | Pássaro e ninho no perfil local, estado do mundo no host (`saves/mundo.txt`, a cada 30 s e ao sair) |
| Record | Total acumulado de cajus entregues |
| Voz | Microfone aberto por detecção de fala, com filtro de voz de pássaro |
| Esforço | Bater os braços livremente, sem barra de energia |
| Mapa | Ilha low-poly gerada por semente, com o mar como limite: quem passa do limite é abocanhado por uma baleia gigante e cuspido de volta (seção 3.7) |

## 3. Mecânicas

### 3.1 Controle pelo corpo

Todas as medidas são divididas pela largura dos ombros, para não depender da distância até a câmera.

| Gesto | Detecção | Efeito |
|---|---|---|
| Bater | Descida rápida dos dois punhos | Empurra o pássaro enquanto as asas descem. Também decola do ninho |
| Planar | Braços abertos na altura dos ombros | Perde altura devagar e mantém a velocidade |
| Subir | Braços erguidos acima dos ombros | Troca velocidade por altitude, de forma proporcional à altura dos braços |
| Mergulhar | Braços abaixados junto ao corpo | Desce e acelera, de forma proporcional à altura dos braços |
| Curvar | Ângulo entre os punhos, com zona morta de 8° e limite de 45° | Inclina e faz a curva como um avião |
| Menu do ninho | Braço esquerdo ou direito erguido por 0,7 s | Escolhe a opção |

As asas vistas em primeira pessoa acompanham a altura de cada braço. Em alta velocidade, rajadas de vento aparecem na tela.

Se o corpo sair do quadro, o pássaro plana nivelado e a tela avisa. A webcam aparece espelhada em uma janela pequena,
com o esqueleto desenhado por cima. Antes de jogar, uma tela de calibração confere se o corpo inteiro está visível
e se as mãos são detectadas nas quatro bordas do quadro espelhado (faixas de 25%). As marcas valem em qualquer ordem,
as duas mãos marcam juntas e cada uma exige ao menos duas poses do detector, tolerando falhas de até 0,3 s. Com tudo
marcado, a tela segue sozinha depois de 1 s.

### 3.2 Ciclo de jogo

1. Voar até um cajueiro. Os cajus nascem nas copas e reaparecem com o tempo.
2. Passar perto de um caju para coletar, até o limite da carga. Começa com 3.
3. Pousar no ninho, que abre o menu automaticamente.
4. Cada caju entregue soma 1 ao total e vira moeda guardada no ninho.
5. Gastar a moeda com **comer** ou **investir**, e decolar batendo os braços.

| Comer: pássaro | Investir: ninho |
|---|---|
| Velocidade | Defesa: aumenta o tempo necessário para roubar |
| Agilidade nas curvas | Alarme: avisa e mostra o ladrão |
| Capacidade de carga | Cofre: parte dos cajus não pode ser roubada |
| Alcance da coleta | Tamanho: ninho maior e bônus de moedas na entrega |

No menu do ninho, a primeira escolha é entre comer (braço esquerdo) e investir (braço direito). Dentro de uma lista, o braço
esquerdo passa para a próxima opção e o direito compra. Bater os braços decola.

| Upgrade | Nível 1 | Nível 2 | Nível 3 | Nível 4 | Nível 5 | Efeito por nível |
|---|---|---|---|---|---|---|
| Velocidade | 3 | 6 | 10 | 15 | 22 | +8% de velocidade |
| Agilidade | 2 | 5 | 9 | 14 | 20 | +12% de agilidade |
| Capacidade | 4 | 8 | 13 | 19 | 26 | +1 caju de carga |
| Alcance | 2 | 4 | 8 | 13 | 18 | +2,5 m de coleta |
| Defesa | 3 | 6 | 10 | 15 | 22 | +1,5 s no tempo de roubo, que começa em 4 s |
| Alarme | 3 | 6 | 10 | 15 | 22 | Avisa o roubo e aponta o ladrão por 5 s, +3 s por nível |
| Cofre | 4 | 8 | 13 | 19 | 26 | Guarda 5 moedas por nível longe dos ladrões |
| Tamanho | 3 | 7 | 12 | 18 | 25 | Ninho 18% maior e +20% de moedas na entrega |

### 3.3 Roubo e colisões

- **Roubo:** o pássaro pousa no ninho de um rival como pousa no próprio, mas sem menu. Uma barra enche e, a cada volta
  completa, leva 1 caju para a carga. A volta dura 4 s, mais 1,5 s por nível de Defesa do dono. O roubo continua até a
  carga encher, o ninho esvaziar ou o ladrão bater os braços para fugir.
- **O que sai do ninho:** o ninho guarda moedas. Cada caju roubado tira 1 moeda da vítima, menos as protegidas pelo
  Cofre (5 por nível). Com Alarme, a vítima ouve uma sirene e a bússola aponta o ladrão por 5 s, mais 3 s por nível.
- **Mergulho:** quem está em mergulho e encosta em outro jogador com carga derruba toda a carga dele.
- **Árvore:** bater com força (a partir de 6 m/s contra a superfície) reduz a velocidade e derruba um caju. Não existe
  morte.
- **Avião no teto:** o voo tem teto em 420 m. Quem passa de 410 m chama um avião, que aparece a 700 m, à frente do
  pássaro e um pouco acima, com o ronco do motor crescendo. Não há aviso na tela: o jogador vê e ouve o avião. Ele voa a
  95 m/s, o dobro do pássaro mais rápido, sempre apontando para o alvo, então sempre chega, depois de uns 6 a 8 s. Ele
  não desce abaixo de 360 m: quem mergulha antes vê o avião passar por cima e ir embora. Se acertar, o pássaro é jogado
  para baixo a 140 m/s e continua despencando durante a tontura (cai mais de 200 m), a tela treme, fica 3 s sem controle
  do voo e toda a carga cai no mundo. A câmera continua em primeira pessoa.
  Depois de cada avião, o mesmo jogador espera 25 s para chamar outro.
- **Cajus caindo:** os cajus derrubados pelo caçador, pelo mergulho, pelo avião ou pela árvore caem até o chão ou uma copa e ficam
  30 s no mundo. Qualquer um pega, como caju comum. Quem derrubou espera 2 s para pegar o próprio caju de volta.
- **Cajus roubados valem moeda, mas não somam no record.** Isso impede dois jogadores de inflarem o total trocando cajus.
- Um ninho só existe no mundo enquanto o dono está conectado, então ninguém é roubado offline. Se o dono sai, quem
  estava roubando decola.
- **Quem decide:** o host confirma cada roubo (o ladrão precisa estar no poleiro, o ninho precisa ter moedas fora do
  Cofre e o intervalo mínimo é de 3 s), detecta os mergulhos e escolhe o id de cada caju caído. O cliente só derruba a
  própria carga, na batida em árvore.
- **Avião em rede:** o dono do pássaro pede o avião ao chegar ao teto. O host confere a altura (com folga de 40 m) e o
  intervalo de 20 s por alvo, numera o avião, escolhe o ponto de partida e confirma para todos. Cada computador simula o
  mesmo avião a partir desse ponto, perseguindo a posição que conhece do alvo, então todos veem o avião e a batida. Só o
  dono decide a batida, com a posição real do próprio pássaro: aplica o tranco na hora e avisa o host, que confere o
  número do avião e derruba a carga do alvo para todos. O perfil é local, então cada computador desconta a moeda do próprio dono.

### 3.4 Pontuação e record

- **Pontuação atual:** total de cajus entregues pelo perfil.
- **Record:** maior total já registrado entre os perfis vistos naquele computador, salvo em `record.txt`.
- **Aviso na tela:** aparece quando o seu total ultrapassa o record. Enquanto você lidera, o HUD mostra "RECORD".

Essa regra mantém sua escolha de total acumulado e dá sentido ao aviso exigido pelo enunciado, porque comparar o total
só com ele mesmo faria o record ser batido a cada caju.

### 3.5 Pássaros procedurais

Cada pássaro nasce de uma semente de 32 bits, que alimenta um genoma determinístico. O genoma usa só o gerador inteiro do jogo e contas básicas, sem trigonometria, para dar o mesmo resultado em qualquer computador:

1. **Corpo:** 9 seções octogonais ao longo do eixo do voo, com cabeça, pescoço, peito, barriga e uropígio de proporções próprias.
2. **Partes:** bico em losango (comprimento, altura e curva), crista (nenhuma, tufo, leque ou penacho para trás), cauda em leque sem sobreposição (leque, bifurcada, cunha, reta, longa ou com fitas) e asas em 4 painéis com ponta fina, redonda ou com dedos.
3. **Encaixe:** cada parte se liga a um ponto da cabeça ou do corpo e escala conforme o tamanho dele.
4. **Cor:** paleta por harmonia (complementar, triádica, complementar dividida, tetrádica, análoga com destaque ou base neutra com dois tons vivos), com contraste forçado entre as duas cores maiores. Padrões sorteados pintam capuz, boné, máscara ou babador, colar, faixa no peito, barra e pontas das asas e faixa na ponta da cauda. O tom principal evita o verde das copas.
5. **Comportamento:** o porte (comprimento e envergadura) define o papel no ecossistema. O host escolhe as sementes dos NPCs de modo que dois deles nunca fiquem parecidos.

| Papel | Quantos | Comportamento |
|---|---|---|
| Bando | 6 pequenos | Voa em bando usando o algoritmo Boids |
| Coletor | 2 médios | Procura e disputa cajus |
| Caçador | 1 grande | Persegue quem carrega 2 cajus ou mais e derruba a carga |
| Curioso | 2 | Acompanha um jogador ao lado e um pouco atrás por alguns segundos e depois se afasta |
| Assustado | 3 pequenos | Fica pousado no alto de uma copa e foge quando alguém chega perto; depois pousa em outra |
| Planador | 2 grandes | Sobe em espiral nas térmicas e plana até a próxima |
| Territorial | 1 médio | Ronda a própria árvore e persegue quem entra perto dela, a alguns metros |
| Migratório | 5 médios | Bando em V que atravessa a ilha de ponta a ponta |

Só o caçador mexe na carga dos jogadores. Quem está pousado no ninho não é atacado. A carga derrubada cai no mundo (seção 3.3). Todo NPC desvia de copas, troncos, chão e mar e nunca fica dentro deles.

Pela rede trafega só a semente, e cada computador reconstrói o mesmo pássaro. O snapshot leva também um byte de comportamento por NPC (papel e pouso), para o cliente animar cada um. As asas têm estado próprio por pássaro: o planeio flexiona e intercala rajadas de batidas, o mergulho recolhe as asas para trás e o pouso as dobra rente ao corpo.

### 3.6 Voz por proximidade

O áudio é capturado em 48 kHz e tratado antes de qualquer envio:

- **Passa-alta de 100 Hz:** remove zumbidos e batidas graves.
- **Redução de ruído espectral:** a cada 10 ms, a FFT separa o som em frequências. O ruído de fundo de cada frequência é
  estimado nos momentos sem voz, e um ganho de Wiener atenua o que é ruído, em até 20 dB, sem apagar a voz.
- **Reconhecimento de voz humana:** a transmissão só começa quando há energia na faixa da fala, entre 300 e 3400 Hz, e o
  som é periódico, como a vibração das cordas vocais. Ventilador, teclado, cliques e portas batendo não abrem o microfone.

Depois disso vem a detecção de fala. Um ganho automático leva a voz a um volume alvo, mesmo com
microfone baixo. Depois a voz recebe o filtro de pássaro, 8 semitons acima, e é comprimida com Opus. O host repassa cada
pacote apenas aos jogadores a até 300 m. Quem recebe guarda a voz em um buffer que cresce sozinho quando a rede fica
irregular, para não cortar, e aplica:

1. **Volume por distância:** voz cheia até 70 m, ainda bem audível a 150 m e silêncio a partir de 300 m, porque os
   pássaros voam rápido.
2. **Posição estéreo:** a voz vem do lado em que o outro pássaro está.
3. **Efeito Doppler:** a voz fica mais aguda quando os pássaros se aproximam e mais grave quando se afastam, calculada pela
   velocidade relativa entre quem fala e quem ouve.

Microfone, codificação, mixagem e saída rodam em uma thread de áudio própria, separada do jogo, para um quadro lento não
cortar a voz. O indicador de fala abre o bico do pássaro. Fones são recomendados para evitar eco.

### 3.7 Limite do mapa: a baleia gigante

O mapa é um círculo de 1150 m de raio, todo cercado de mar. Não existe parede: quem passa do limite sobre o mar é
abocanhado por uma baleia gigante, que o cospe de volta para a ilha.

- **Aviso (de 1050 a 1150 m):** a sombra da baleia ronda por baixo da água, perto do pássaro, cada vez maior. Sobem bolhas,
  ela espirra pelo espiráculo (um sopro forte com garoa caindo) e um ronco grave e áspero fica mais alto. Não há aviso
  na tela, só esses sinais no mundo. Dá tempo de virar.
- **Câmera:** do salto até o fim do arremesso, a câmera sai para a terceira pessoa. Com o pássaro na boca, ela fica de
  lado, entre a baleia e a ilha. Num salto alto ela se afasta (até uns 3 vezes) e olha para baixo enquanto a baleia
  sobe, para mostrar o arco, e volta a se aproximar quando a baleia chega e cai com o pássaro. No arremesso, segue atrás
  e acima do pássaro. Depois volta suavemente para a primeira pessoa.
- **Salto e mordida:** passou de 1150 m, a baleia salta do mar de boca aberta, vindo de fora do mapa, cantando um gemido
  grave. O salto é balístico: sai de 20 m de fundo, sobe em arco freando e chega ao ápice exatamente na altura do
  pássaro, onde fecha a boca, em qualquer altura até o teto de 420 m. O pássaro não é puxado: ele perde o embalo em
  cerca de meio segundo e paira virado para a baleia, com um ajuste suave de poucos metros ao longo do salto para ficar
  entre as mandíbulas. Saltos maiores levam mais tempo: cerca de 1 s até 75 m de altura e 3,7 s do teto (a gravidade do
  salto cai de 100 para 65 m/s² nos saltos altos, para que se veja o arco).
- **Queda e mastigada:** a baleia cai de volta com o pássaro na boca. O corpo bate na água e freia a cabeça até a altura de
  mastigar; o splash (espuma, gotas e, nas quedas altas, uma coluna de água e um ronco por baixo) cresce com a velocidade
  da pancada. Ela mastiga 1,4 s com a cabeça fora da água. A câmera treme na mordida, na pancada e a cada mastigada, e os
  comandos não respondem. Do teto, a sequência até a cuspida leva uns 9 s; de baixo, uns 3,5 s.
- **Cuspida:** depois de voltar para a água, a baleia cospe o pássaro com força para o centro da ilha, a 115 m/s e
  subindo. Cuspir de volta no mar, e não no ar, mantém a mastigada visível, faz a cuspida sair sempre da mesma altura
  (o arremesso chega sempre ao mesmo ponto, venha o pássaro de onde vier) e evita jogá-lo de volta perto do teto, onde
  chamaria o avião. Ele voa arremessado por
  2,5 s sem perder velocidade nem responder aos comandos, avança mais de 200 m para dentro do limite e só então o jogador
  volta a controlar o voo, ainda rápido, chegando a uns 450 m da borda.
- **Carga:** os cajus caem no mar com a mordida e se perdem. Diferente da carga derrubada por mergulho ou por árvore
  (seção 3.3), eles não ficam no mundo: o mar está fora do mapa e ninguém consegue alcançá-los. A perda também impede que a
  baleia vire um atalho gratuito, já que a cuspida leva o pássaro rápido até o meio da ilha.
- **Cooldown e garantias:** depois de cuspir, a baleia espera 8 s antes de atacar o mesmo pássaro de novo, e a autoridade
  só aceita outra mordida depois que o pássaro voltou para dentro do raio de aviso. Nesse intervalo o vento antigo volta a
  empurrar o pássaro para a ilha, e uma parede invisível a 1330 m impede que alguém se perca no mar.
- **Quem decide:** o dono do pássaro pede a mordida (evento `WhaleBite`) quando passa do limite. A autoridade (o host, ou a
  `LocalAuthority` no jogo sozinho) confere se o pássaro está mesmo além do limite, se ainda não foi mordido nesta saída e
  quantos cajus ele levava, e envia o evento a todos. O evento leva o alvo e a posição da mordida. Cada computador anima a
  mesma baleia a partir do instante em que recebe o evento, sem mais nada pela rede: a pose depende só da posição e do
  tempo, e a duração de cada fase sai da altura da mordida (`whaleTimeline`), que o host também usa para proteger o
  pássaro durante a sequência. O dono controla o próprio pássaro durante a sequência, preso na boca e depois arremessado. Os outros desenham o
  pássaro entre as mandíbulas enquanto ele está preso.

## 4. Arquitetura

```
Thread da câmera  -> PoseDetector -> GestureRecognizer -> FlightIntent
Thread principal  -> Game -> World, Renderer, HUD
Thread de rede    -> NetSession (UDP) <-> Snapshots e eventos
Thread de áudio   -> VoiceChat (SDL2 + Opus)
```

| Módulo | Classes principais |
|---|---|
| `vision/` | `Camera`, `PoseDetector`, `GestureRecognizer`, `VisionPipeline` |
| `input/` | `FlightInput`, `BodyInput`, `KeyboardInput`, `GestureControls` |
| `core/` | `Game` |
| `world/` | `World`, `Island`, `CashewTree` |
| `entity/` | `PlayerBird`, `FlightModel` |
| `game/` | `GameplaySession`, `CashewField`, `CashewCollector`, `NestMenu` |
| `events/` | `BoundaryWhale` (baleia do limite do mapa) e `WhaleGate` (regra da autoridade para a mordida) |
| `save/` | `Profile`, `ProfileStore`, `WorldStore`, `RecordManager` |
| `ui/` | `GameUi`, `ScreenFlow`, `CalibrationTracker`, `SoundManager` |
| `procedural/` | `buildBirdMesh` e `buildBirdFlightParts` |
| `ai/` | `NpcEcosystem` |
| `events/` | `SkyPlane`, `SkyPlaneLedger` e `SkyPlaneEvent`: o avião do teto, com modelo e sons próprios |
| `net/` | `UdpSession`, `Protocol`, `HostAuthority`, `ClientAuthority`, `RemoteTrack` e `NetMatch` (implementa `NetSession`) |
| `gfx/` | `Renderer`, `Shader`, `Mesh`, `Camera3D`, `BirdViewModel` |
| `shared/` | Contratos entre as frentes e `LocalAuthority` |
| `voice/` | `VoiceChat`, `SpeechDetector`, `PitchShifter`, `VoiceEncoder`, `VoiceDecoder`, `VoiceMixer` |

**Rede:** o host decide cajus, NPCs, roubos, colisões e as mordidas da baleia. Cada cliente simula o próprio voo para responder sem atraso. O
host envia o estado 20 vezes por segundo e os clientes interpolam os outros pássaros.

## 5. Requisitos do enunciado

| # | Requisito | Como será atendido | Situação |
|---|---|---|---|
| 1 | Diagrama UML de classes | `docs/uml_classes.png`, gerado a partir da arquitetura final | Em produção |
| 2 | Bastante métodos | Classes pequenas por módulo | Atendido |
| 3 | Rodar até sair, com menu | Menu principal: hospedar, entrar, calibrar, perfil, sair | Atendido |
| 4 | Pontuação, record e arquivo | Seção 3.4, com `record.txt` | Atendido |
| 5 | Imagens com fundo transparente | Caju, ícones de upgrades e HUD em PNG com alfa | Atendido |
| 6 | OpenCV, YOLO ou OpenPose | YOLOv8n-pose executado pelo OpenCV DNN | Atendido |
| 7 | Jogabilidade | Calibração, zonas mortas e suavização dos gestos | Atendido |
| 8 | Vídeo espelhado | Imagem e gestos tratados em modo espelho | Atendido |
| 9 | Detecção em todo o vídeo | Teste de cobertura nas bordas durante a calibração | Atendido |
| 10 | Som em eventos | Coleta, entrega, roubo, alarme, upgrade, record e voo (vento de fundo, chiado leve pela velocidade, sopro grave na subida, assobio de mergulho, sopro a cada batida de asa e mar perto da costa), avião no teto (motor que se aproxima, com volume pela distância e efeito Doppler, e estrondo da batida) e baleia do limite (sons longos e graves em camadas: ronco áspero e espirro no aviso, canto de baleia no salto, splash pesado com cauda de água, mordida com estalo, mastigadas, cuspida com sopro forte). Os NPCs cantam só quando estão a menos de 80 m, com canto próprio por porte | Atendido |
| Instrução 4 | Indicar uso de IA | [`USO_DE_IA.md`](USO_DE_IA.md) | Atendido |

## 6. Cronograma

As tarefas, os responsáveis e as dependências estão em [`DIVISAO_TRABALHO.md`](DIVISAO_TRABALHO.md).

| Marco | Situação em 23/11/2026 | O que falta |
|---|---|---|
| **M0** Prova técnica | Concluído | Nada |
| **M1** Voo | Concluído | Nada |
| **M2** Ciclo de jogo | Concluído e validado no jogo real | Nada |
| **M3** Multiplayer | Salas de até 6 jogadores, roubo e mergulho, testados entre computadores | Nada |
| **M4** Pássaros | Gerador, ecossistema e NPCs em rede | Nada |
| **M5** Voz | Voz por proximidade com Doppler e redução de ruído | Nada |
| **M6** Fechamento | Teste integrado feito. README, créditos e uso de IA escritos | Diagrama UML |

Se o prazo apertar, os cortes seguem esta ordem: filtro de voz, caçadores, alarme e cofre do ninho, sons de ambiente.

## 7. Riscos

| Risco | Mitigação |
|---|---|
| Pose lenta na CPU, sem GPU dedicada | Medir no M0, reduzir a entrada do modelo e rodar em thread própria |
| Hotspot do celular bloqueia a descoberta automática de salas | Testar antes no mesmo hotspot e manter um campo para digitar o IP do host |
| Gestos confundidos entre si | Máquina de estados com histerese e ajuste com pessoas diferentes |
| Eco na voz | Fones e detecção de fala |
| Escopo grande para 19 dias | Marcos independentes, com jogo funcional ao fim de cada um |
| Daniel pausou e restou uma pessoa | Tarefas dele assumidas pelo Neto, com subagents em paralelo |
| Teste de rede depende de um segundo computador | Testes automáticos em uma máquina e um segundo computador combinado para a F1 |

## 8. Pendências

O tema "Outro" foi aprovado pelo professor. O jogo integrado, a N5 e os testes de rede entre computadores já foram
validados (ver [`RESULTADOS_TESTE_REDE.md`](RESULTADOS_TESTE_REDE.md)).

1. Produzir o diagrama UML. O README, os [créditos](CREDITOS.md) e o [uso de IA](USO_DE_IA.md) já estão escritos.
