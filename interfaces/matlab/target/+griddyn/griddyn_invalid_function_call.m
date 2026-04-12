function v = griddyn_invalid_function_call()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = griddynMEX(0, 10);
  end
  v = vInitialized;
end
