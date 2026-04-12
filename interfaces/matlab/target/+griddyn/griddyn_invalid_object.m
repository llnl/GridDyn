function v = griddyn_invalid_object()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = griddynMEX(0, 1);
  end
  v = vInitialized;
end
