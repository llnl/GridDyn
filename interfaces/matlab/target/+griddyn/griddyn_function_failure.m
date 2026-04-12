function v = griddyn_function_failure()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = griddynMEX(0, 11);
  end
  v = vInitialized;
end
