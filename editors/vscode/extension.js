const path = require('path');
const { workspace, ExtensionContext } = require('vscode');
const {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind
} = require('vscode-languageclient/node');

let client;

function activate(context) {
    const serverOptions = {
        command: 'unfish',
        args: ['lsp'],
        transport: TransportKind.stdio
    };

    const clientOptions = {
        documentSelector: [{ scheme: 'file', language: 'unfish' }],
        synchronize: {
            fileEvents: workspace.createFileSystemWatcher('**/*.unfish')
        }
    };

    client = new LanguageClient(
        'unfishLanguageServer',
        'Unfish Language Server',
        serverOptions,
        clientOptions
    );

    client.start();
}

function deactivate() {
    if (!client) {
        return undefined;
    }
    return client.stop();
}

module.exports = {
    activate,
    deactivate
};
