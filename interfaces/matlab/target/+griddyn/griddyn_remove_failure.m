function v = griddyn_remove_failure()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = griddynMEX(0, 5);
  end
  v = vInitialized;
end
