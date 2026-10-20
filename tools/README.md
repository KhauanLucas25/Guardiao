## Configurador MQTT local

O arquivo `tools/configurar.html` gera localmente o JSON de configuração usado pelo ESP32 para conectar ao AWS IoT Core.

O usuário informa endpoint, certificado do dispositivo, chave privada e certificado raiz. Esses dados são usados apenas no navegador para gerar a configuração; não são armazenados no arquivo, enviados a um servidor ou versionados no Git.