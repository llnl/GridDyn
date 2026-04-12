function v = griddyn_add_failure()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = griddynMEX(0, 4);
  end
  v = vInitialized;
end
