async function HandleRequest(executionMetadata, ...input) {
  logMessage('Handling request');
  const module = await getModule();
  logMessage('Done loading WASM Module');

  // Pass in the getValues function for the C++ code to call.
  const result = module.handleRequestCc(getValues, input);
  logMessage('handleRequestCc result: ' + JSON.stringify(result));
  return result;
}
