# Birds&Cashews: créditos e licenças

Este documento lista o material de terceiros usado pelo jogo, de onde ele veio e sob qual licença. O resumo dos arquivos
de `assets/` também está em [`assets/CREDITOS.txt`](../assets/CREDITOS.txt).

O repositório ainda não define uma licença para o código do jogo.

## 1. Arquivos incluídos no repositório

| Arquivo | Origem | Licença |
|---|---|---|
| `assets/font.ttf` | Poppins Bold, versão 4.004. Copyright 2020 The Poppins Project Authors ([itfoundry/Poppins](https://github.com/itfoundry/Poppins)) | SIL Open Font License 1.1, texto completo em [`assets/OFL-Poppins.txt`](../assets/OFL-Poppins.txt) |
| `models/pose_trimmed.onnx` | YOLOv8n-pose, da [Ultralytics](https://github.com/ultralytics/ultralytics), exportado para ONNX | AGPL-3.0, a mesma do repositório da Ultralytics |
| `assets/*.png` | Imagens originais do projeto, geradas por [`tools/gerar_assets.py`](../tools/gerar_assets.py) | Do projeto |

### Fonte Poppins

A OFL permite usar, estudar, modificar e redistribuir a fonte, inclusive junto com um programa, desde que a fonte não seja
vendida sozinha e que o aviso de copyright e a licença acompanhem o arquivo. Por isso `OFL-Poppins.txt` fica ao lado de
`font.ttf`.

### Modelo de pose

O jogo usa o YOLOv8n-pose, a versão menor do modelo de pose da Ultralytics, treinado para detectar 17 pontos do corpo. O
OpenCV DNN executa o modelo na CPU, com entrada de 320 x 320 pixels. O nome `pose_trimmed` indica que o arquivo termina
antes da decodificação final da rede: as caixas, a confiança e os pontos são decodificados em C++, em
`src/vision/PoseDetector.cpp`.

O arquivo entrou no repositório no commit `7d84bf2`, em 18/11/2026, e foi substituído no commit `fb2c645`, no mesmo dia.
O repositório não guarda o script nem a versão do pacote `ultralytics` usados na exportação.

A Ultralytics distribui o código e os pesos do YOLOv8 sob a [AGPL-3.0](https://www.gnu.org/licenses/agpl-3.0.html) e
oferece uma licença comercial à parte. A AGPL exige que quem distribui o modelo, ou um programa que o usa, ofereça o
código-fonte correspondente sob termos compatíveis.

### Imagens

O caju, a moeda, o ninho, a seta da bússola, o selo de record e os ícones dos upgrades foram desenhados por código, sem
material de terceiros. Para gerar de novo:

```bash
python3 tools/gerar_assets.py   # requer Pillow
```

## 2. Material gerado pelo próprio jogo

| Item | Como é feito |
|---|---|
| Sons dos menus, da coleta, da entrega, dos upgrades, do record, do pouso e da decolagem | Sintetizados em tempo de execução por `src/ui/SoundManager.cpp`, com ondas e envelopes |
| Ilha e cajueiros | Gerados por semente em `src/world/` |
| Pássaros | Gerados por semente em `src/procedural/` |
| Voz de pássaro | Processamento da voz do jogador em `src/voice/` |

## 3. Bibliotecas usadas na compilação

As bibliotecas não fazem parte do repositório. Elas são instaladas pelo sistema e ligadas ao executável.

| Biblioteca | Uso | Licença |
|---|---|---|
| [OpenCV](https://opencv.org) 4 | Webcam, imagem e DNN | Apache 2.0 a partir da versão 4.5 |
| [SDL2](https://www.libsdl.org) | Janela, teclado, som e microfone | zlib |
| [Opus](https://opus-codec.org) | Compressão da voz | BSD de 3 cláusulas |
| [GLM](https://github.com/g-truc/glm) | Matemática 3D | MIT |
| OpenGL | Desenho 3D | Fornecido pelo driver de vídeo |
